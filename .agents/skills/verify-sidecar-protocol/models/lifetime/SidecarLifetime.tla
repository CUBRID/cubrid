-------------------------- MODULE SidecarLifetime --------------------------
EXTENDS Integers, Sequences, FiniteSets, TLC

CONSTANTS MaxOps, WriterCount, KeepSnapshot, UseViews, AllowCancel,
          AllowAbort, Savepoint, Fault

Writers == 1..WriterCount
Ids == 0..(MaxOps + 1)
Parts == 1..2
NoRef == -1
EmptyChunk == [present |-> FALSE, value |-> 0, ins |-> 0, del |-> 0]
InitialChunk == [present |-> TRUE, value |-> 0, ins |-> 0, del |-> 0]
EmptyContext == [kind |-> "none", old |-> NoRef, new |-> NoRef, value |-> NoRef, completed |-> FALSE]
EmptyEvent == [kind |-> "none", id |-> 0, part |-> 1, prior |-> EmptyChunk]
EmptyView == [open |-> FALSE, ref |-> NoRef, expected |-> NoRef,
              materialized |-> FALSE, value |-> NoRef]
EmptyPrefix == [ref |-> NoRef, value |-> NoRef]

VARIABLES chunks, parents, logical, txStatus, commitAt, clock, phase, owner,
          admitted, context, journal, stmtMark, saveMark, saveRolled,
          cancelled, abortStarted, nextId, issued, snapshot, view, viewUsed,
          milestones, pulse, prefix
vars == <<chunks, parents, logical, txStatus, commitAt, clock, phase, owner,
          admitted, context, journal, stmtMark, saveMark, saveRolled,
          cancelled, abortStarted, nextId, issued, snapshot, view, viewUsed,
          milestones, pulse, prefix>>

Family(id) == CASE id \in {1, 3} -> 1 [] id \in {2, 4} -> 2 [] OTHER -> 0
Nested(w) == w + 2
VisibleInsert(id, reader, cut) ==
  id = 0 \/ (Family(id) = reader /\ reader # 0 /\ txStatus[reader] = "active")
         \/ (Family(id) # 0 /\ txStatus[Family(id)] = "committed"
             /\ commitAt[Family(id)] <= cut)
VisibleDelete(id, reader, cut) == id # 0 /\ VisibleInsert(id, reader, cut)
ChunkVisible(id, part, reader, cut) ==
  /\ id \in Ids
  /\ chunks[id][part].present
  /\ VisibleInsert(chunks[id][part].ins, reader, cut)
  /\ ~VisibleDelete(chunks[id][part].del, reader, cut)
Resolve(id, reader, cut, expected) ==
  id \in Ids /\ \A p \in Parts :
    ChunkVisible(id, p, reader, cut) /\ chunks[id][p].value = expected

LastVisible(seq, reader, cut) ==
  CHOOSE i \in 1..Len(seq) :
    VisibleInsert(seq[i].ins, reader, cut)
    /\ \A j \in (i + 1)..Len(seq) : ~VisibleInsert(seq[j].ins, reader, cut)
Parent(reader, cut) == parents[LastVisible(parents, reader, cut)]
Expected(reader, cut) == logical[LastVisible(logical, reader, cut)].value
ObservationOK(reader, cut) ==
  LET p == Parent(reader, cut) IN
    IF Expected(reader, cut) = NoRef THEN p.ref = NoRef
    ELSE p.ref # NoRef /\ Resolve(p.ref, reader, cut, Expected(reader, cut))

Init ==
  /\ chunks = [id \in Ids |-> [p \in Parts |-> IF id = 0 THEN InitialChunk ELSE EmptyChunk]]
  /\ parents = <<[ref |-> 0, ins |-> 0]>>
  /\ logical = <<[value |-> 0, ins |-> 0]>>
  /\ txStatus = [w \in 1..2 |-> IF w = 1 THEN "active" ELSE IF WriterCount = 2 THEN "pending" ELSE "absent"]
  /\ commitAt = [w \in 1..2 |-> 0]
  /\ clock = 0
  /\ phase = [w \in 1..2 |-> IF w = 1 THEN "ready" ELSE IF WriterCount = 2 THEN "idle" ELSE "absent"]
  /\ owner = 1
  /\ admitted = [w \in 1..2 |-> 0]
  /\ context = [w \in 1..2 |-> EmptyContext]
  /\ journal = [w \in 1..2 |-> <<>>]
  /\ stmtMark = [w \in 1..2 |-> 0]
  /\ saveMark = [w \in 1..2 |-> -1]
  /\ saveRolled = [w \in 1..2 |-> FALSE]
  /\ cancelled = [w \in 1..2 |-> FALSE]
  /\ abortStarted = [w \in 1..2 |-> FALSE]
  /\ nextId = 1
  /\ issued = <<0>>
  /\ snapshot = [active |-> KeepSnapshot, cut |-> 0]
  /\ view = EmptyView
  /\ viewUsed = FALSE
  /\ milestones = {}
  /\ pulse = FALSE
  /\ prefix = [w \in 1..2 |-> [stmt |-> EmptyPrefix, save |-> EmptyPrefix, check |-> "none"]]

NeedSaveUndo(w) == w = 1 /\ Savepoint /\ admitted[w] >= 3
  /\ saveMark[w] >= 0 /\ ~saveRolled[w]
ViewBoundaryReady(w) == w # 1 \/ ~view.open \/ view.materialized \/ Fault = "DeferredView"
Limit(w) == IF w = 1 THEN MaxOps ELSE 1
AllowedKind(w, kind) ==
  IF w = 2 THEN kind = "replace"
  ELSE IF admitted[w] + 1 \in {1, 2} THEN kind = "replace"
  ELSE IF admitted[w] + 1 = 3 THEN kind \in {"hot", "delete"}
  ELSE kind = "replace"

Join ==
  /\ WriterCount = 2 /\ phase[2] = "idle"
  /\ phase' = [phase EXCEPT ![2] = "waiting"]
  /\ milestones' = IF owner = 1 THEN milestones \cup {"contended"} ELSE milestones
  /\ UNCHANGED <<chunks, parents, logical, txStatus, commitAt, clock, owner,
                 admitted, context, journal, stmtMark, saveMark, saveRolled,
                 cancelled, abortStarted, nextId, issued, snapshot, view, viewUsed, pulse, prefix>>

Acquire ==
  /\ WriterCount = 2 /\ phase[2] = "waiting"
  /\ owner = 0 \/ Fault = "StealLock"
  /\ IF Fault = "NoProgress" /\ owner = 0
        THEN /\ pulse' = ~pulse
             /\ UNCHANGED <<phase, txStatus, owner>>
        ELSE /\ phase' = [phase EXCEPT ![2] = "ready"]
             /\ txStatus' = [txStatus EXCEPT ![2] = "active"]
             /\ owner' = 2
             /\ UNCHANGED pulse
  /\ UNCHANGED <<chunks, parents, logical, commitAt, clock, admitted, context,
                 journal, stmtMark, saveMark, saveRolled, cancelled, abortStarted,
                 nextId, issued, snapshot, view, viewUsed, milestones, prefix>>

DiscardView ==
  /\ view.open
  /\ view' = [view EXCEPT !.open = FALSE]
  /\ UNCHANGED <<chunks, parents, logical, txStatus, commitAt, clock, phase, owner,
                 admitted, context, journal, stmtMark, saveMark, saveRolled, cancelled,
                 abortStarted, nextId, issued, snapshot, viewUsed, milestones, pulse, prefix>>

OpenView ==
  /\ UseViews /\ ~viewUsed /\ phase[1] = "ready" /\ txStatus[1] = "active"
  /\ Parent(1, clock).ref # NoRef
  /\ view' = [open |-> TRUE, ref |-> Parent(1, clock).ref,
              expected |-> Expected(1, clock), materialized |-> FALSE, value |-> NoRef]
  /\ viewUsed' = TRUE
  /\ UNCHANGED <<chunks, parents, logical, txStatus, commitAt, clock, phase, owner,
                 admitted, context, journal, stmtMark, saveMark, saveRolled, cancelled,
                 abortStarted, nextId, issued, snapshot, milestones, pulse, prefix>>

Materialize ==
  /\ view.open /\ ~view.materialized
  /\ Resolve(view.ref, 1, clock, view.expected)
  /\ view' = [view EXCEPT !.materialized = TRUE,
                    !.value = IF Fault = "CorruptCopy" THEN chunks[view.ref][1].value + 1
                              ELSE chunks[view.ref][1].value]
  /\ UNCHANGED <<chunks, parents, logical, txStatus, commitAt, clock, phase, owner,
                 admitted, context, journal, stmtMark, saveMark, saveRolled, cancelled,
                 abortStarted, nextId, issued, snapshot, viewUsed, milestones, pulse, prefix>>

Begin(w, kind) ==
  /\ phase[w] = "ready" /\ admitted[w] < Limit(w) /\ ~NeedSaveUndo(w)
  /\ AllowedKind(w, kind)
  /\ ViewBoundaryReady(w)
  /\ LET old == Parent(w, clock).ref
         new == IF kind = "replace" THEN IF Fault = "ReuseId" THEN IF old = NoRef THEN 0 ELSE old ELSE nextId ELSE old
         val == IF kind = "replace" THEN nextId ELSE IF kind = "delete" THEN NoRef ELSE Expected(w, clock)
     IN /\ context' = [context EXCEPT ![w] = [kind |-> kind, old |-> old, new |-> new, value |-> val, completed |-> FALSE]]
        /\ IF kind = "replace"
              THEN /\ nextId' = nextId + 1
                   /\ issued' = Append(issued, new)
              ELSE /\ UNCHANGED <<nextId, issued>>
  /\ view' = IF w = 1 /\ Fault # "DeferredView" THEN [view EXCEPT !.open = FALSE] ELSE view
  /\ milestones' = IF w = 1 /\ view.open /\ view.materialized
                       THEN milestones \cup {"viewBeforeMutation"} ELSE milestones
  /\ prefix' = [prefix EXCEPT ![w].stmt = [ref |-> Parent(w, clock).ref, value |-> Expected(w, clock)],
                            ![w].check = "none"]
  /\ stmtMark' = [stmtMark EXCEPT ![w] = Len(journal[w])]
  /\ admitted' = [admitted EXCEPT ![w] = @ + 1]
  /\ phase' = [phase EXCEPT ![w] = IF kind = "replace" THEN "insert1"
                                     ELSE IF kind = "delete" THEN "delete1" ELSE "publish"]
  /\ UNCHANGED <<chunks, parents, logical, txStatus, commitAt, clock, owner,
                 journal, saveMark, saveRolled, cancelled, abortStarted, snapshot,
                 viewUsed, pulse>>

ChunkInsert(w, part) ==
  /\ phase[w] = IF part = 1 THEN "insert1" ELSE "insert2"
  /\ LET id == context[w].new
         creator == IF Fault = "WrongFamily" /\ w = 1 /\ part = 1 THEN 4 ELSE Nested(w)
         e == [kind |-> "chunk", id |-> id, part |-> part, prior |-> chunks[id][part]]
     IN /\ chunks' = [chunks EXCEPT ![id][part] = [present |-> TRUE,
                                                   value |-> IF Fault = "WrongChunkData" /\ part = 1
                                                            THEN context[w].value + 1 ELSE context[w].value,
                                                   ins |-> creator, del |-> 0]]
        /\ journal' = [journal EXCEPT ![w] = Append(@, e)]
  /\ phase' = [phase EXCEPT ![w] = IF part = 1 THEN "insert2" ELSE "delete1"]
  /\ UNCHANGED <<parents, logical, txStatus, commitAt, clock, owner, admitted,
                 context, stmtMark, saveMark, saveRolled, cancelled, abortStarted,
                 nextId, issued, snapshot, view, viewUsed, milestones, pulse, prefix>>

ChunkDelete(w, part) ==
  /\ phase[w] = IF part = 1 THEN "delete1" ELSE "delete2"
  /\ LET id == context[w].old IN
     IF id = NoRef
       THEN /\ UNCHANGED <<chunks, journal>>
       ELSE /\ LET e == [kind |-> "delete", id |-> id, part |-> part, prior |-> chunks[id][part]]
               IN /\ chunks' = [chunks EXCEPT ![id][part].del = Nested(w)]
                  /\ journal' = [journal EXCEPT ![w] = Append(@, e)]
  /\ phase' = [phase EXCEPT ![w] = IF part = 1 THEN "delete2" ELSE "publish"]
  /\ UNCHANGED <<parents, logical, txStatus, commitAt, clock, owner, admitted,
                 context, stmtMark, saveMark, saveRolled, cancelled, abortStarted,
                 nextId, issued, snapshot, view, viewUsed, milestones, pulse, prefix>>

Publish(w) ==
  /\ phase[w] = "publish"
  /\ parents' = Append(parents, [ref |-> IF context[w].kind = "delete" THEN NoRef
                                        ELSE IF Fault = "RewriteHot" /\ context[w].kind = "hot" THEN 0 ELSE context[w].new,
                                 ins |-> IF Fault = "WrongParentFamily" /\ w = 1 THEN 2 ELSE w])
  /\ journal' = [journal EXCEPT ![w] = Append(@, [EmptyEvent EXCEPT !.kind = "parent"])]
  /\ phase' = [phase EXCEPT ![w] = "finish"]
  /\ UNCHANGED <<chunks, logical, txStatus, commitAt, clock, owner, admitted,
                 context, stmtMark, saveMark, saveRolled, cancelled, abortStarted,
                 nextId, issued, snapshot, view, viewUsed, milestones, pulse, prefix>>

Finish(w) ==
  /\ phase[w] = "finish"
  /\ logical' = Append(logical, [value |-> context[w].value, ins |-> w])
  /\ context' = [context EXCEPT ![w].completed = TRUE]
  /\ journal' = [journal EXCEPT ![w] = Append(@, [EmptyEvent EXCEPT !.kind = "logical"])]
  /\ prefix' = IF w = 1 /\ admitted[w] = 1
       THEN [prefix EXCEPT ![w].save = [ref |-> IF context[w].kind = "delete" THEN NoRef ELSE context[w].new,
                                       value |-> context[w].value]] ELSE prefix
  /\ saveMark' = IF w = 1 /\ admitted[w] = 1
                    THEN [saveMark EXCEPT ![w] = Len(journal[w]) + 1] ELSE saveMark
  /\ milestones' = IF w # 1 THEN milestones
       ELSE milestones \cup {IF admitted[w] = 1 THEN "replace1"
                             ELSE IF admitted[w] = 2 THEN "replace2"
                             ELSE IF admitted[w] = 3 THEN context[w].kind
                             ELSE "continued"}
  /\ phase' = [phase EXCEPT ![w] = "ready"]
  /\ UNCHANGED <<chunks, parents, txStatus, commitAt, clock, owner, admitted,
                 stmtMark, saveRolled, cancelled, abortStarted, nextId,
                 issued, snapshot, view, viewUsed, pulse>>

Cancel(w) ==
  /\ AllowCancel /\ ~cancelled[w] /\ w = 1
  /\ phase[w] \in {"insert2", "delete1", "delete2", "publish", "finish"}
  /\ phase' = [phase EXCEPT ![w] = "undoStmt"]
  /\ cancelled' = [cancelled EXCEPT ![w] = TRUE]
  /\ milestones' = IF phase[w] = "finish" THEN milestones \cup {"cancelledPublished"} ELSE milestones
  /\ UNCHANGED <<chunks, parents, logical, txStatus, commitAt, clock, owner,
                 admitted, context, journal, stmtMark, saveMark, saveRolled,
                 abortStarted, nextId, issued, snapshot, view, viewUsed, pulse, prefix>>

StartSaveUndo(w) ==
  /\ phase[w] = "ready" /\ NeedSaveUndo(w)
  /\ view' = IF w = 1 /\ Fault # "RollbackRetainsView" THEN [view EXCEPT !.open = FALSE] ELSE view
  /\ phase' = [phase EXCEPT ![w] = "undoSave"]
  /\ UNCHANGED <<chunks, parents, logical, txStatus, commitAt, clock, owner,
                 admitted, context, journal, stmtMark, saveMark, saveRolled,
                 cancelled, abortStarted, nextId, issued, snapshot, viewUsed, milestones, pulse, prefix>>

StartAbort(w) ==
  /\ AllowAbort /\ phase[w] = "ready" /\ admitted[w] > 0 /\ ~abortStarted[w]
  /\ view' = IF w = 1 /\ Fault # "RollbackRetainsView" THEN [view EXCEPT !.open = FALSE] ELSE view
  /\ phase' = [phase EXCEPT ![w] = "undoTx"]
  /\ abortStarted' = [abortStarted EXCEPT ![w] = TRUE]
  /\ milestones' = IF w = 1 /\ view.open /\ ~view.materialized
                       THEN milestones \cup {"abortedLazyView"} ELSE milestones
  /\ UNCHANGED <<chunks, parents, logical, txStatus, commitAt, clock, owner,
                 admitted, context, journal, stmtMark, saveMark, saveRolled,
                 cancelled, nextId, issued, snapshot, viewUsed, pulse, prefix>>

UndoTarget(w) == IF phase[w] = "undoStmt" THEN IF Fault = "WrongStmtTarget" THEN 0 ELSE stmtMark[w]
                  ELSE IF phase[w] = "undoSave" THEN IF Fault = "WrongSaveTarget" THEN 0 ELSE saveMark[w] ELSE 0
Pop(s) == SubSeq(s, 1, Len(s) - 1)
UndoEffect(w) ==
  /\ phase[w] \in {"undoStmt", "undoSave", "undoTx"}
  /\ Len(journal[w]) > UndoTarget(w)
  /\ LET e == journal[w][Len(journal[w])] IN
     /\ IF e.kind \in {"chunk", "delete"}
          THEN /\ chunks' = IF (Fault = "PartialUndo" /\ e.kind = "delete" /\ e.part = 1)
                                      \/ (Fault = "SkipInsertUndo" /\ e.kind = "chunk" /\ e.part = 1)
                                 THEN chunks ELSE [chunks EXCEPT ![e.id][e.part] = e.prior]
               /\ UNCHANGED <<parents, logical>>
          ELSE IF e.kind = "parent"
            THEN /\ parents' = Pop(parents) /\ UNCHANGED <<chunks, logical>>
            ELSE /\ logical' = Pop(logical) /\ UNCHANGED <<chunks, parents>>
  /\ journal' = [journal EXCEPT ![w] = Pop(@)]
  /\ UNCHANGED <<txStatus, commitAt, clock, phase, owner, admitted, context,
                 stmtMark, saveMark, saveRolled, cancelled, abortStarted,
                 nextId, issued, snapshot, view, viewUsed, milestones, pulse, prefix>>

FinishUndo(w) ==
  /\ phase[w] \in {"undoStmt", "undoSave", "undoTx"}
  /\ Len(journal[w]) = UndoTarget(w)
  /\ IF phase[w] = "undoTx"
        THEN /\ phase' = [phase EXCEPT ![w] = "done"]
             /\ txStatus' = [txStatus EXCEPT ![w] = "aborted"]
             /\ owner' = 0
        ELSE /\ phase' = [phase EXCEPT ![w] = "ready"]
             /\ UNCHANGED <<txStatus, owner>>
  /\ prefix' = [prefix EXCEPT ![w].check = IF phase[w] = "undoStmt" THEN "stmt"
                                               ELSE IF phase[w] = "undoSave" THEN "save" ELSE "none"]
  /\ saveRolled' = IF phase[w] = "undoSave" THEN [saveRolled EXCEPT ![w] = TRUE] ELSE saveRolled
  /\ context' = [context EXCEPT ![w].completed = FALSE]
  /\ UNCHANGED <<chunks, parents, logical, commitAt, clock, admitted,
                 journal, stmtMark, saveMark, cancelled, abortStarted, nextId,
                 issued, snapshot, view, viewUsed, milestones, pulse>>

Commit(w) ==
  /\ phase[w] = "ready" /\ admitted[w] = Limit(w) /\ ~NeedSaveUndo(w)
  /\ view' = IF w = 1 THEN [view EXCEPT !.open = FALSE] ELSE view
  /\ phase' = [phase EXCEPT ![w] = "done"]
  /\ txStatus' = [txStatus EXCEPT ![w] = "committed"]
  /\ commitAt' = [commitAt EXCEPT ![w] = clock + 1]
  /\ clock' = clock + 1
  /\ owner' = 0
  /\ UNCHANGED <<chunks, parents, logical, admitted, context, journal, stmtMark,
                 saveMark, saveRolled, cancelled, abortStarted, nextId, issued,
                 snapshot, viewUsed, milestones, pulse, prefix>>

ReleaseSnapshot ==
  /\ snapshot.active
  /\ snapshot' = [snapshot EXCEPT !.active = FALSE]
  /\ UNCHANGED <<chunks, parents, logical, txStatus, commitAt, clock, phase, owner,
                 admitted, context, journal, stmtMark, saveMark, saveRolled, cancelled,
                 abortStarted, nextId, issued, view, viewUsed, milestones, pulse, prefix>>

Eligible(id, p) ==
  LET d == chunks[id][p].del IN
    /\ chunks[id][p].present /\ d # 0
    /\ txStatus[Family(d)] = "committed"
    /\ ~snapshot.active \/ commitAt[Family(d)] <= snapshot.cut \/ Fault = "PrematureGC"
Collect ==
  \E id \in Ids, p \in Parts :
    /\ Eligible(id, p)
    /\ chunks' = IF Fault = "CollectorLivelock" THEN chunks
                  ELSE [chunks EXCEPT ![id][p].present = FALSE]
    /\ pulse' = IF Fault = "CollectorLivelock" THEN ~pulse ELSE pulse
    /\ UNCHANGED <<parents, logical, txStatus, commitAt, clock, phase, owner,
                   admitted, context, journal, stmtMark, saveMark, saveRolled, cancelled,
                   abortStarted, nextId, issued, snapshot, view, viewUsed, milestones, prefix>>

Worker(w) ==
  \/ IF w = 2 THEN Join \/ Acquire ELSE FALSE
  \/ \E kind \in {"replace", "hot", "delete"} : Begin(w, kind)
  \/ ChunkInsert(w, 1) \/ ChunkInsert(w, 2)
  \/ ChunkDelete(w, 1) \/ ChunkDelete(w, 2)
  \/ Publish(w) \/ Finish(w) \/ Cancel(w) \/ StartSaveUndo(w) \/ StartAbort(w)
  \/ UndoEffect(w) \/ FinishUndo(w) \/ Commit(w)
Done ==
  /\ \A w \in Writers : phase[w] = "done"
  /\ ~snapshot.active
  /\ ~\E id \in Ids, p \in Parts : Eligible(id, p)
  /\ UNCHANGED vars
Next == Worker(1) \/ (IF WriterCount = 2 THEN Worker(2) ELSE FALSE)
        \/ OpenView \/ Materialize \/ DiscardView \/ ReleaseSnapshot \/ Collect \/ Done
Spec == Init /\ [][Next]_vars /\ WF_vars(Worker(1)) /\ WF_vars(Worker(2))
        /\ WF_vars(Materialize) /\ WF_vars(DiscardView) /\ WF_vars(ReleaseSnapshot) /\ WF_vars(Collect)

ValueDomain == {NoRef} \cup 0..(MaxOps + 2)
ChunkType(c) ==
  /\ c.present \in BOOLEAN /\ c.value \in 0..(MaxOps + 2)
  /\ c.ins \in 0..4 /\ c.del \in 0..4
EventType(e) ==
  /\ e.kind \in {"chunk", "delete", "parent", "logical"}
  /\ e.id \in Ids /\ e.part \in Parts /\ ChunkType(e.prior)
TypeOK ==
  /\ DOMAIN chunks = Ids
  /\ \A id \in Ids : DOMAIN chunks[id] = Parts /\ \A p \in Parts : ChunkType(chunks[id][p])
  /\ Len(parents) >= 1 /\ Len(logical) >= 1
  /\ \A i \in 1..Len(parents) : parents[i].ref \in (Ids \cup {NoRef}) /\ parents[i].ins \in 0..4
  /\ \A i \in 1..Len(logical) : logical[i].value \in ValueDomain /\ logical[i].ins \in 0..4
  /\ DOMAIN txStatus = 1..2 /\ DOMAIN commitAt = 1..2 /\ DOMAIN phase = 1..2
  /\ DOMAIN admitted = 1..2 /\ DOMAIN context = 1..2 /\ DOMAIN journal = 1..2
  /\ DOMAIN stmtMark = 1..2 /\ DOMAIN saveMark = 1..2 /\ DOMAIN saveRolled = 1..2
  /\ DOMAIN cancelled = 1..2 /\ DOMAIN abortStarted = 1..2 /\ DOMAIN prefix = 1..2
  /\ \A w \in 1..2 :
       /\ txStatus[w] \in {"active", "pending", "committed", "aborted", "absent"}
       /\ commitAt[w] \in 0..WriterCount
       /\ phase[w] \in {"idle", "waiting", "ready", "insert1", "insert2", "delete1", "delete2",
                         "publish", "finish", "undoStmt", "undoSave", "undoTx", "done", "absent"}
       /\ admitted[w] \in 0..Limit(w)
       /\ context[w].kind \in {"none", "replace", "hot", "delete"}
       /\ context[w].old \in (Ids \cup {NoRef}) /\ context[w].new \in (Ids \cup {NoRef})
       /\ context[w].value \in ValueDomain /\ context[w].completed \in BOOLEAN
       /\ \A i \in 1..Len(journal[w]) : EventType(journal[w][i])
       /\ stmtMark[w] \in 0..(6 * MaxOps + 6) /\ saveMark[w] \in -1..(6 * MaxOps + 6)
       /\ saveRolled[w] \in BOOLEAN /\ cancelled[w] \in BOOLEAN /\ abortStarted[w] \in BOOLEAN
       /\ prefix[w].check \in {"none", "stmt", "save"}
       /\ \A mark \in {"stmt", "save"} :
            /\ prefix[w][mark].ref \in (Ids \cup {NoRef}) /\ prefix[w][mark].value \in ValueDomain
  /\ owner \in 0..WriterCount /\ clock \in 0..WriterCount /\ nextId \in 1..(MaxOps + 2)
  /\ Len(issued) \in 1..(MaxOps + 2) /\ \A i \in 1..Len(issued) : issued[i] \in Ids
  /\ snapshot.active \in BOOLEAN /\ snapshot.cut = 0
  /\ view.open \in BOOLEAN /\ view.materialized \in BOOLEAN /\ viewUsed \in BOOLEAN
  /\ view.ref \in (Ids \cup {NoRef}) /\ view.expected \in ValueDomain /\ view.value \in ValueDomain
  /\ milestones \subseteq {"contended", "replace1", "replace2", "hot", "delete", "continued",
                           "cancelledPublished", "viewBeforeMutation", "abortedLazyView"}
  /\ pulse \in BOOLEAN

VisibleValueComplete ==
  /\ ObservationOK(0, clock)
  /\ snapshot.active => ObservationOK(0, snapshot.cut)
  /\ \A w \in Writers : phase[w] = "ready" => ObservationOK(w, clock)
HotReuse == \A w \in Writers :
  (phase[w] = "ready" /\ context[w].kind = "hot" /\ context[w].completed)
  => Parent(w, clock).ref = context[w].old
FreshIdentity == \A i, j \in 1..Len(issued) : i # j => issued[i] # issued[j]
ViewStable ==
  /\ view.materialized => view.value = view.expected
  /\ view.open /\ ~view.materialized => Resolve(view.ref, 1, clock, view.expected)
OwnerCorrect == \A w \in Writers :
  phase[w] \in {"ready", "insert1", "insert2", "delete1", "delete2", "publish",
                "finish", "undoStmt", "undoSave", "undoTx"} => owner = w
AtQuiescence ==
  /\ \A w \in Writers : phase[w] = "done"
  /\ ~snapshot.active
  /\ ~\E id \in Ids, p \in Parts : Eligible(id, p)
NoUnownedAfterDrain == AtQuiescence =>
  \A id \in Ids, p \in Parts : chunks[id][p].present => Parent(0, clock).ref = id
RollbackPrefixRestored == \A w \in Writers :
  (phase[w] = "ready" /\ prefix[w].check # "none") =>
    LET saved == prefix[w][prefix[w].check] IN
      /\ Parent(w, clock).ref = saved.ref
      /\ Expected(w, clock) = saved.value
      /\ saved.ref # NoRef => Resolve(saved.ref, w, clock, saved.value)
CleanupPending == \E id \in Ids, p \in Parts :
  LET c == chunks[id][p] IN
    c.present /\ c.del # 0 /\ txStatus[Family(c.del)] = "committed"
EventuallyAcquired == [](phase[2] = "waiting" => <>(phase[2] # "waiting"))
EventuallyDrained == [](CleanupPending => <>AtQuiescence)

NoPendingCleanupWitness == ~(CleanupPending /\ ~snapshot.active
                             /\ \A w \in Writers : phase[w] = "done")
NoDrainedWitness == ~(AtQuiescence /\ \A p \in Parts : ~chunks[0][p].present)
NoStmtPrefixWitness == ~(phase[1] = "ready" /\ prefix[1].check = "stmt"
                         /\ prefix[1].stmt.ref \notin {0, NoRef})
NoSavePrefixWitness == ~(phase[1] = "ready" /\ prefix[1].check = "save"
                         /\ prefix[1].save.ref \notin {0, NoRef})
NoContentionWitness == ~(phase[2] = "waiting" /\ owner = 1)
NoHotScheduleWitness == ~(/\ txStatus[1] = "committed" /\ admitted[1] >= 4
                          /\ saveRolled[1] /\ {"replace1", "replace2", "hot", "continued", "contended", "viewBeforeMutation"} \subseteq milestones
                          /\ phase[2] = "waiting" /\ snapshot.active
                          /\ viewUsed /\ view.materialized /\ ~view.open)
NoDeleteScheduleWitness == ~(/\ txStatus[1] = "committed" /\ admitted[1] >= 4
                             /\ saveRolled[1] /\ {"replace1", "replace2", "delete", "continued", "contended", "viewBeforeMutation"} \subseteq milestones
                             /\ phase[2] = "waiting" /\ snapshot.active
                             /\ viewUsed /\ view.materialized /\ ~view.open)
NoCancelWitness == ~("cancelledPublished" \in milestones /\ phase[1] = "ready")
NoAbortLazyViewWitness == ~("abortedLazyView" \in milestones /\ txStatus[1] = "aborted")
NoReclaimWitness == ~(txStatus[1] = "committed" /\ ~snapshot.active
                      /\ ~chunks[0][1].present /\ chunks[0][1].del # 0)
NoSnapshotWitness == ~(txStatus[1] = "committed" /\ snapshot.active
                       /\ chunks[0][1].present /\ chunks[0][1].del # 0 /\ ~Eligible(0, 1))
=============================================================================
