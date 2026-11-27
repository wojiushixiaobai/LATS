#!/usr/bin/env python3
"""Model-check the sysflagtb in-flight deferred-flags recovery protocol.

The model mirrors the invariants implemented by the production code:

  * every TB entry executes "addi.d $ra,$zero,0" before guest code, so $ra is
    clean (None) on any entry that is not intercepted by the HAMT
    invalid-entry handler;
  * a deferred edge whose successor redefines all flag-defining state emits a
    pcaddi $ra marker right before the direct-link slot and clears it again on
    the materialisation fallback and on the pending-IRQ path;
  * the HAMT handler runs *instead of* the target entry when the target TB was
    poisoned, and must recover the predecessor's deferred flags before the C
    lookup can observe them;
  * the handler may only trust a marker whose source TB is still deferred, is
    not invalidated/corrupted at the record level, and whose recorded edge
    targets the poisoned TB.  A structurally-valid marker with inconsistent
    metadata is a fatal protocol bug, not a normal miss.

The checker enumerates short event sequences over a small TB graph, with
poisoning optionally racing an already-executed deferred jump, and asserts
that no observer can sample stale flags and that no stale marker survives a
normal entry.
"""
from itertools import product

USE_SAFE = 0      # successor's first guest instruction defines all six flags
USE_READ = 0x3f   # successor observes incoming flags

# TBs: A is a deferred source with two edges, S0/S1 are its safe successors,
# R is a flag-reading TB (never a legal deferred target), X is unrelated.
TBS = ('A', 'S0', 'S1', 'R', 'X')
# Edge table: (source, edge) -> (successor, deferred-capable source?)
EDGES = {
    ('A', 0): ('S0', True),
    ('A', 1): ('S1', True),
    ('S0', 0): ('X', False),
    ('S1', 0): ('X', False),
    ('X', 0): ('A', False),
    ('R', 0): ('X', False),
}
SAFE = {'S0': True, 'S1': True}
# Immutable, translation-time published metadata consulted by the HAMT
# handler (latx_sysflagtb_recovery_edge): the marker word, skip bits and
# jmp_reset_offset relationship.  Separated from the link veto so that a
# corrupted record can be modelled as a fatal handler bug.
HANDLER_SAFE = {'S0': True, 'S1': True}


def link_allowed(src, edge, target):
    """Mirror of latx_tb_eflag_link_allowed() for this model."""
    _, deferred = EDGES[(src, edge)]
    if not deferred:
        return True  # eager edge: no marker, no veto needed
    return SAFE.get(target, False)


class ProtocolBug(Exception):
    pass


def run(events):
    """Execute one abstract schedule; returns (observed_flags_seen, recovered)."""
    ra = None                 # marker: (src, edge) or None
    deferred_pending = False  # flags of the last guest instruction are deferred
    invalid = set()
    recovered = 0
    observations = []

    def enter(target):
        nonlocal ra, deferred_pending
        if target in invalid:
            nonlocal recovered
            # HAMT invalid-entry handler.  The target TB must be a legal
            # deferred target; anything else means the source materialised.
            if target in SAFE:
                if ra is None:
                    # No in-flight deferred jump: the flags were produced by a
                    # non-deferred source path before entering C.
                    if deferred_pending:
                        raise ProtocolBug(
                            'stale deferred flags reach handler without marker')
                else:
                    src, edge = ra
                    if EDGES[(src, edge)][0] != target:
                        raise ProtocolBug('marker precondition violated')
                    if not link_allowed(src, edge, target):
                        raise ProtocolBug('recovered a vetoed link')
                    # latx_sysflagtb_recovery_edge(): structural match.
                    if not (EDGES[(src, edge)][1] and
                            HANDLER_SAFE.get(target, False)):
                        raise ProtocolBug('recovered with inconsistent metadata')
                    deferred_pending = False
                    recovered += 1
                ra = None
                return
            # Target is not a legal deferred successor: a deferred link into it
            # would have violated the link veto.
            if ra is not None:
                raise ProtocolBug('deferred marker entered an unsafe target')
            return
        # Normal entry: entry-clear runs first.
        ra = None
        # A safe successor's first guest instruction redefines all six flags;
        # every other entry had its producer materialised by the source.
        deferred_pending = False

    def execute_edge(src, edge, poison_race=False):
        nonlocal ra, deferred_pending
        target, deferred = EDGES[(src, edge)]
        if deferred and not link_allowed(src, edge, target):
            # Link vetoed or no direct link: the fallback materialises the
            # flags and the marker/entry-clear path clears $ra.
            ra = None
            deferred_pending = False
            enter(target)
            return
        if deferred:
            # Deferred direct link: pcaddi $ra marker immediately before the
            # slot, then the target entry either runs its clear or is
            # intercepted by the handler when poisoned.
            ra = (src, edge)
            deferred_pending = True
            if poison_race:
                invalid.add(target)
            enter(target)
        else:
            # Eager edge: producer executed inline, no marker.
            ra = None
            deferred_pending = False
            enter(target)

    for kind, *args in events:
        if kind == 'exec':
            execute_edge(*args)
        elif kind == 'exec_deferred_poisoned':
            execute_edge(*args, poison_race=True)
        elif kind == 'poison':
            (target,) = args
            invalid.add(target)
        elif kind == 'revalidate':
            (target,) = args
            invalid.discard(target)
        elif kind == 'observe':
            # A signal/exception observes EFLAGS only after the producer ran.
            if deferred_pending:
                raise ProtocolBug('observer sampled deferred flags')
            observations.append(True)
        else:
            raise AssertionError(kind)

    if ra is not None:
        raise ProtocolBug('stale marker survived across entries: %r' % (ra,))
    return observations, recovered


def main():
    # Event alphabet: every edge execution, every poison target, revalidate,
    # and an observer event.  Deferred edges are executed both normally and
    # with the poison race.
    alphabet = []
    for src, edge in EDGES:
        alphabet.append(('exec', src, edge))
        if EDGES[(src, edge)][1]:
            alphabet.append(('exec_deferred_poisoned', src, edge))
    for t in TBS:
        alphabet.append(('poison', t))
        alphabet.append(('revalidate', t))
    alphabet.append(('observe',))

    checked = 0
    recovered_paths = 0
    for depth in range(0, 5):
        for seq in product(alphabet, repeat=depth):
            events = [e if isinstance(e, tuple) else (e,) for e in seq]
            try:
                _, recovered = run(events)
            except ProtocolBug as exc:
                raise SystemExit('FAIL %r: %s' % (events, exc))
            checked += 1
            if recovered:
                recovered_paths += 1
    assert checked > 100000
    assert recovered_paths > 0, 'recovery was never exercised'

    # A marker whose metadata is inconsistent must be fatal, not silently
    # trusted.  Model the corruption by removing the target from the set of
    # published safe successors; the handler must then abort rather than
    # recover with a stale/mismatched producer.
    global HANDLER_SAFE, SAFE
    saved_handler, saved_link = HANDLER_SAFE, SAFE
    # Keep the link veto consistent so the deferred link is actually taken,
    # but corrupt the handler-side published metadata.
    HANDLER_SAFE = {}
    try:
        run([('exec_deferred_poisoned', 'A', 0)])
    except ProtocolBug:
        pass
    else:
        raise SystemExit('FAIL: inconsistent marker was trusted')
    finally:
        HANDLER_SAFE, SAFE = saved_handler, saved_link

    print('sysflagtb recovery protocol model: PASS '
          '(%d schedules, %d recovery paths)' % (checked, recovered_paths))


if __name__ == '__main__':
    main()
