------------------------- MODULE HeapVacuumReuse -------------------------
EXTENDS Naturals, TLC
CONSTANTS Target, HomeRelease, ForwardRelease
VARIABLES pc, slot, latch, logs, durable, sysDurable, inserted, committed,
          crashed, restart, bad
vars == <<pc, slot, latch, logs, durable, sysDurable, inserted, committed,
          crashed, restart, bad>>
Init == /\ pc = 0 /\ slot = [p \in {"home", "forward"} |-> "old"]
        /\ latch = [p \in {"home", "forward"} |-> TRUE]
        /\ logs = 0 /\ durable = 0 /\ sysDurable = FALSE
        /\ inserted = FALSE /\ committed = FALSE /\ crashed = FALSE
        /\ restart = FALSE /\ bad = FALSE
HomeVacuum == /\ ~crashed /\ pc = 0
              /\ slot' = [slot EXCEPT !["home"] = "free"]
              /\ pc' = 1 /\ UNCHANGED <<latch, logs, durable, sysDurable, inserted, committed, crashed, restart, bad>>
HomeDirty == /\ ~crashed /\ pc = 1
             /\ latch' = [latch EXCEPT !["home"] = ~HomeRelease]
             /\ pc' = 2 /\ UNCHANGED <<slot, logs, durable, sysDurable, inserted, committed, crashed, restart, bad>>
HomeLog == /\ ~crashed /\ pc = 2 /\ logs' = 1 /\ pc' = 3
           /\ UNCHANGED <<slot, latch, durable, sysDurable, inserted, committed, crashed, restart, bad>>
ForwardVacuum == /\ ~crashed /\ pc = 3
                 /\ slot' = [slot EXCEPT !["forward"] = "free"] /\ pc' = 4
                 /\ UNCHANGED <<latch, logs, durable, sysDurable, inserted, committed, crashed, restart, bad>>
ForwardLog == /\ ~crashed /\ pc = 4 /\ logs' = 2 /\ pc' = 5
              /\ UNCHANGED <<slot, latch, durable, sysDurable, inserted, committed, crashed, restart, bad>>
ForwardUnfix == /\ ~crashed /\ pc = 5 /\ ForwardRelease
                /\ latch' = [latch EXCEPT !["forward"] = FALSE] /\ pc' = 6
                /\ UNCHANGED <<slot, logs, durable, sysDurable, inserted, committed, crashed, restart, bad>>
SysopCommit == /\ ~crashed /\ ((pc = 5 /\ ~ForwardRelease) \/ pc = 6)
               /\ pc' = 7 /\ UNCHANGED <<slot, latch, logs, durable, sysDurable, inserted, committed, crashed, restart, bad>>
PostCommitUnfix == /\ ~crashed /\ pc = 7
                   /\ latch' = [p \in {"home", "forward"} |-> FALSE] /\ pc' = 8
                   /\ UNCHANGED <<slot, logs, durable, sysDurable, inserted, committed, crashed, restart, bad>>
Insert == /\ ~crashed /\ ~inserted /\ ~latch[Target] /\ slot[Target] = "free"
          /\ slot' = [slot EXCEPT ![Target] = "insert"] /\ inserted' = TRUE
          /\ UNCHANGED <<pc, latch, logs, durable, sysDurable, committed, crashed, restart, bad>>
InsertCommit == /\ ~crashed /\ inserted /\ ~committed
                /\ committed' = TRUE /\ durable' = logs
                /\ sysDurable' = (pc >= 7)
                /\ UNCHANGED <<pc, slot, latch, logs, inserted, crashed, restart, bad>>
Crash == /\ ~crashed /\ \E d \in durable..logs:
           /\ durable' = d /\ crashed' = TRUE
           /\ slot' = IF inserted /\ ~committed THEN [slot EXCEPT ![Target] = "free"] ELSE slot
           /\ UNCHANGED <<pc, latch, logs, sysDurable, inserted, committed, restart, bad>>
Restart == /\ crashed /\ ~restart
           /\ restart' = TRUE
           /\ bad' = ~sysDurable /\ durable >= (IF Target = "home" THEN 1 ELSE 2)
                     /\ slot[Target] = "insert" /\ committed
           /\ UNCHANGED <<pc, slot, latch, logs, durable, sysDurable, inserted, committed, crashed>>
Next == HomeVacuum \/ HomeDirty \/ HomeLog \/ ForwardVacuum \/ ForwardLog \/ ForwardUnfix
        \/ SysopCommit \/ PostCommitUnfix \/ Insert \/ InsertCommit \/ Crash \/ Restart
Spec == Init /\ [][Next]_vars
UndoFindsEmptySlot == ~bad
=============================================================================
