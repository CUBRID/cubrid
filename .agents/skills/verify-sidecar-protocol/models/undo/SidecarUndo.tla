---------------------------- MODULE SidecarUndo ----------------------------
EXTENDS Naturals, Sequences, FiniteSets, TLC
CONSTANTS MaxSlots, Capacity, Policy, Fault
ASSUME /\ MaxSlots >= 2 /\ Capacity >= 2
       /\ Policy \in {"reservation", "latch"}
       /\ Fault \in {"none", "slot", "bytes", "page", "clobber", "livelock", "noLatch", "noReservation",
                    "dropLatch", "dropReservation", "unavailable"}
Slots == 1..MaxSlots
Elements(seq) == {seq[i] : i \in 1..Len(seq)}
VARIABLES phase, page, competitor, wal, durable, latch, reservation,
          recoveryStep, violations, turn, crashed, saw
vars == <<phase, page, competitor, wal, durable, latch, reservation,
          recoveryStep, violations, turn, crashed, saw>>
NoViolations == [slot |-> FALSE, bytes |-> FALSE, page |-> FALSE, committed |-> FALSE]
Init ==
 /\ phase = "ready"
 /\ page = [allocated |-> TRUE, generation |-> 1,
             occupants |-> [s \in Slots |-> IF s = 1 THEN "old" ELSE "empty"], used |-> 2]
 /\ competitor = [status |-> "none", slot |-> 0, size |-> 0]
 /\ wal = <<>> /\ durable = 0 /\ latch = FALSE /\ reservation = FALSE
 /\ recoveryStep = "none" /\ violations = NoViolations /\ turn = FALSE
 /\ crashed = FALSE /\ saw = [recovery |-> FALSE, committedBeforeEnd |-> FALSE,
                               endLost |-> FALSE, endCovered |-> FALSE, shared |-> FALSE, loserUndo |-> FALSE]
BeginDelete ==
 /\ phase = "ready"
 /\ phase' = "running"
 /\ page' = [page EXCEPT !.occupants[1] = "empty", !.used = 0]
 /\ wal' = Append(wal, "delete") /\ durable' = 1
 /\ latch' = (Policy = "latch" /\ Fault # "noLatch")
 /\ reservation' = (Policy = "reservation" /\ Fault # "noReservation")
 /\ UNCHANGED <<competitor, recoveryStep, violations, turn, crashed, saw>>
Insert(s, n) ==
 /\ phase \in {"running", "ended", "rollback"} /\ competitor.status = "none"
 /\ page.allocated /\ ~latch /\ page.occupants[s] = "empty"
 /\ n \in 1..2 /\ page.used + n <= Capacity
 /\ (~reservation \/
      /\ (s # 1 \/ Fault \in {"slot", "clobber"})
      /\ (page.used + n + 2 <= Capacity \/ Fault = "bytes"))
 /\ page' = [page EXCEPT !.occupants[s] = "competitor", !.used = @ + n]
 /\ competitor' = [status |-> "inserted", slot |-> s, size |-> n]
 /\ wal' = Append(wal, "insert")
 /\ saw' = [saw EXCEPT !.shared = @ \/ phase = "running"]
 /\ UNCHANGED <<phase, durable, latch, reservation, recoveryStep, violations, turn, crashed>>
CommitCompetitor ==
 /\ phase \in {"running", "ended", "rollback"} /\ competitor.status = "inserted"
 /\ competitor' = [competitor EXCEPT !.status = "committed"]
 /\ wal' = Append(wal, "commit") /\ durable' = Len(wal')
 /\ saw' = [saw EXCEPT !.committedBeforeEnd = @ \/ phase = "running",
                          !.endCovered = @ \/ phase = "ended"]
 /\ UNCHANGED <<phase, page, latch, reservation, recoveryStep, violations, turn, crashed>>
AppendEnd ==
 /\ phase = "running"
 /\ phase' = "ended" /\ wal' = Append(wal, "end")
 /\ latch' = FALSE /\ reservation' = FALSE
 /\ UNCHANGED <<page, competitor, durable, recoveryStep, violations, turn, crashed, saw>>
Abort ==
 /\ phase = "running" /\ phase' = "rollback"
 /\ UNCHANGED <<page, competitor, wal, durable, latch, reservation, recoveryStep, violations, turn, crashed, saw>>
DropProtection ==
 /\ phase = "rollback"
 /\ \/ /\ Fault = "dropLatch" /\ latch /\ latch' = FALSE /\ UNCHANGED reservation
    \/ /\ Fault = "dropReservation" /\ reservation /\ reservation' = FALSE /\ UNCHANGED latch
 /\ UNCHANGED <<phase, page, competitor, wal, durable, recoveryStep, violations, turn, crashed, saw>>
Deallocate ==
 /\ phase \in {"running", "rollback"} /\ page.allocated
 /\ \A s \in Slots: page.occupants[s] = "empty"
 /\ (~latch /\ ~reservation) \/ Fault = "page"
 /\ page' = [page EXCEPT !.allocated = FALSE, !.generation = @ + 1]
 /\ UNCHANGED <<phase, competitor, wal, durable, latch, reservation, recoveryStep, violations, turn, crashed, saw>>
Flush ==
 /\ phase \in {"running", "ended"} /\ durable < Len(wal)
 /\ durable' = Len(wal)
 /\ UNCHANGED <<phase, page, competitor, wal, latch, reservation, recoveryStep, violations, turn, crashed, saw>>
Crash ==
 /\ phase \in {"running", "ended"} /\ ~crashed
 /\ phase' = "recovery" /\ recoveryStep' = "loser" /\ crashed' = TRUE
 /\ wal' = SubSeq(wal, 1, durable)
 /\ IF competitor.status = "inserted" /\ ~("insert" \in Elements(wal'))
       THEN /\ page' = [page EXCEPT !.occupants[competitor.slot] = "empty", !.used = @ - competitor.size]
            /\ competitor' = [competitor EXCEPT !.status = "removed"]
       ELSE /\ UNCHANGED <<page, competitor>>
 /\ latch' = FALSE /\ reservation' = FALSE
 /\ saw' = [saw EXCEPT !.recovery = TRUE,
                          !.endLost = @ \/ (phase = "ended" /\ ~("end" \in Elements(SubSeq(wal, 1, durable))))]
 /\ UNCHANGED <<durable, violations, turn>>
UndoImage ==
 /\ violations' = [slot |-> violations.slot \/ page.occupants[1] # "empty",
                    bytes |-> violations.bytes \/ page.used + 2 > Capacity,
                    page |-> violations.page \/ ~page.allocated \/ page.generation # 1,
                    committed |-> violations.committed \/
                      (competitor.status = "committed" /\ competitor.slot = 1)]
 /\ page' = [page EXCEPT !.occupants[1] = "old", !.used = @ + 2]
Rollback ==
 /\ phase = "rollback" /\ UndoImage
 /\ phase' = "done" /\ latch' = FALSE /\ reservation' = FALSE
 /\ UNCHANGED <<competitor, wal, durable, recoveryStep, turn, crashed, saw>>
RecoverLoser ==
 /\ phase = "recovery" /\ recoveryStep = "loser"
 /\ Fault # "unavailable"
 /\ IF Fault = "livelock"
       THEN /\ turn' = ~turn
            /\ UNCHANGED <<page, competitor, recoveryStep, saw>>
       ELSE /\ IF competitor.status = "inserted"
                  THEN /\ page' = [page EXCEPT !.occupants[competitor.slot] = "empty",
                                                !.used = @ - competitor.size]
                       /\ competitor' = [competitor EXCEPT !.status = "removed"]
                  ELSE /\ UNCHANGED <<page, competitor>>
            /\ recoveryStep' = "undo"
            /\ saw' = [saw EXCEPT !.loserUndo = @ \/ (competitor.status = "inserted" /\ ~("end" \in Elements(wal)))]
            /\ UNCHANGED turn
 /\ UNCHANGED <<phase, wal, durable, latch, reservation, violations, crashed>>
RecoverUndo ==
 /\ phase = "recovery" /\ recoveryStep = "undo"
 /\ IF "end" \in Elements(wal)
       THEN /\ UNCHANGED <<page, violations>>
       ELSE UndoImage
 /\ recoveryStep' = "finish"
 /\ UNCHANGED <<phase, competitor, wal, durable, latch, reservation, turn, crashed, saw>>
RecoverFinish ==
 /\ phase = "recovery" /\ recoveryStep = "finish"
 /\ phase' = "done" /\ recoveryStep' = "none"
 /\ UNCHANGED <<page, competitor, wal, durable, latch, reservation, violations, turn, crashed, saw>>
Recover == RecoverLoser \/ RecoverUndo \/ RecoverFinish
Finish ==
 /\ phase = "ended" /\ competitor.status # "inserted" /\ phase' = "done"
 /\ UNCHANGED <<page, competitor, wal, durable, latch, reservation, recoveryStep, violations, turn, crashed, saw>>
AbortCompetitor ==
 /\ phase = "ended" /\ competitor.status = "inserted"
 /\ page' = [page EXCEPT !.occupants[competitor.slot] = "empty", !.used = @ - competitor.size]
 /\ competitor' = [competitor EXCEPT !.status = "removed"]
 /\ UNCHANGED <<phase, wal, durable, latch, reservation, recoveryStep, violations, turn, crashed, saw>>
Done == phase = "done" /\ UNCHANGED vars
Next == BeginDelete \/ (\E s \in Slots, n \in 1..2: Insert(s,n)) \/ CommitCompetitor
        \/ AppendEnd \/ Abort \/ DropProtection \/ Deallocate \/ Flush \/ Crash \/ Rollback \/ Recover
        \/ Finish \/ AbortCompetitor \/ Done
Spec == Init /\ [][Next]_vars /\ WF_vars(Recover)
TypeOK ==
 /\ phase \in {"ready", "running", "ended", "rollback", "recovery", "done"}
 /\ page.allocated \in BOOLEAN /\ page.generation \in 1..2
 /\ page.occupants \in [Slots -> {"old", "empty", "competitor"}]
 /\ page.used \in 0..(Capacity + 2)
 /\ competitor.status \in {"none", "inserted", "committed", "removed"}
 /\ durable \in 0..Len(wal) /\ latch \in BOOLEAN /\ reservation \in BOOLEAN
UndoSlotSafe == ~violations.slot
UndoBytesSafe == ~violations.bytes
UndoPageSafe == ~violations.page
CommittedPreserved == ~violations.committed /\
 (competitor.status = "committed" =>
   /\ page.allocated /\ page.generation = 1
   /\ page.occupants[competitor.slot] = "competitor")
RecoveryAvailable == phase = "recovery" => ENABLED Recover
RecoveryFinishes == [](phase = "recovery" => <> (phase = "done"))
NoRecoveryWitness == ~saw.recovery
NoBeforeEndWitness == ~saw.committedBeforeEnd
NoEndLostWitness == ~saw.endLost
NoEndCoveredWitness == ~saw.endCovered
NoSharedWitness == ~saw.shared
NoLoserUndoWitness == ~saw.loserUndo
=============================================================================
