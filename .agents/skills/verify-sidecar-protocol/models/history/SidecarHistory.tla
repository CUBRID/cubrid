-------------------------- MODULE SidecarHistory --------------------------
EXTENDS Naturals, Sequences, FiniteSets, TLC

CONSTANTS MaxOps, Parts, RollbackAt, SourceCrashes, ReplicaCrashes, Consumers, Fault

Ops == 1..MaxOps
Readers == 1..Consumers
ImageParts == 2 * Parts
Kinds == {"padding", "fragment", "manifest", "cancel", "commit", "abort"}
MaxRecords == 2 + MaxOps * (ImageParts + 1) + 2

BeforeValue(o) == IF RollbackAt > 0 /\ o = RollbackAt + 1 THEN RollbackAt - 1 ELSE o - 1
Token(o, p) == <<o, IF p <= Parts THEN "before" ELSE "after",
                 ((p - 1) % Parts) + 1, IF p <= Parts THEN BeforeValue(o) ELSE o>>
Image(o) == [p \in 1..ImageParts |-> Token(o, p)]
Tokens == {Token(o, p) : o \in Ops, p \in 1..ImageParts}
Refs(o) == {<<o, p>> : p \in 1..ImageParts}
Record(k, o, p, bytes, deps, first) ==
  [kind |-> k, op |-> o, part |-> p, data |-> bytes, refs |-> deps, first |-> first]
Marker(k, o) == Record(k, o, 0, <<>>, {}, 0)
SwapSides(p) == IF p <= Parts THEN p + Parts ELSE p - Parts
Fragment(o, p) == Record("fragment", o, p,
                         Token(o, IF Fault = "writer_swapped" THEN SwapSides(p) ELSE p), {}, 0)
DecodePart(p) == IF Fault = "decoder_swapped" THEN SwapSides(p)
                 ELSE IF Fault = "decoder_reordered" THEN ImageParts + 1 - p ELSE p
ReadParts == IF Fault = "decoder_missing" THEN ImageParts - 1 ELSE ImageParts

VARIABLES wal, durable, archive, source, pins, installed, events, scan,
          offsets, history, replica, witnesses

vars == <<wal, durable, archive, source, pins, installed, events, scan,
          offsets, history, replica, witnesses>>

Positions(k, n) == {i \in 1..n : wal[i].kind = k}
FirstPos(o) ==
  LET ps == {i \in 1..Len(wal) : wal[i].kind = "fragment" /\ wal[i].op = o}
  IN IF ps = {} THEN 0 ELSE CHOOSE i \in ps : \A j \in ps : i <= j
ManifestPos(o) ==
  LET ps == {i \in 1..Len(wal) : wal[i].kind = "manifest" /\ wal[i].op = o}
  IN IF ps = {} THEN 0 ELSE CHOOSE i \in ps : \A j \in ps : i <= j
FragmentPos(o, p) ==
  LET ps == {i \in 1..Len(wal) : wal[i].kind = "fragment" /\ wal[i].op = o /\ wal[i].part = p}
  IN IF ps = {} THEN 0 ELSE CHOOSE i \in ps : \A j \in ps : i <= j
DurableCommit == Positions("commit", durable) # {}
DurableAbort == Positions("abort", durable) # {}
DurableCancels == {wal[i].op : i \in Positions("cancel", durable)}
AvailableCancels == {wal[i].op : i \in {j \in Positions("cancel", durable) : j > archive}}
Ordered(set) == [i \in 1..Cardinality(set) |->
                  CHOOSE o \in set : Cardinality({p \in set : p < o}) = i - 1]
ExpectedEvents == IF DurableCommit THEN Ordered(Ops \ DurableCancels) ELSE <<>>
AllReadersDone == \A c \in Readers : offsets[c] = Len(ExpectedEvents)
HistoryDrained == DurableCommit /\ scan > MaxOps /\ AllReadersDone
HistoryNeeded == ~DurableAbort /\ (~DurableCommit \/ scan <= MaxOps \/ ~AllReadersDone)
RequiredPositions ==
  IF HistoryNeeded THEN {i \in 1..Len(wal) : wal[i].kind # "padding"} ELSE {}

ProtectedOps == IF Fault = "cancel_watermark" /\ DurableCommit THEN Ops \ DurableCancels ELSE Ops
ProtectedPositions ==
  {FirstPos(o) : o \in pins \cap ProtectedOps} \cup
  {IF Fault = "manifest_watermark" THEN ManifestPos(o) ELSE wal[ManifestPos(o)].first : o \in installed \cap ProtectedOps}
RetentionFloor == IF ProtectedPositions = {} THEN Len(wal) + 1
                  ELSE CHOOSE i \in ProtectedPositions : \A j \in ProtectedPositions : i <= j

ManifestComplete(i) ==
  /\ wal[i].refs = Refs(wal[i].op)
  /\ wal[i].first = FirstPos(wal[i].op)
  /\ wal[i].first > 0
  /\ \A p \in 1..ImageParts :
       \E j \in 1..(i - 1) :
         /\ wal[j].kind = "fragment" /\ wal[j].op = wal[i].op /\ wal[j].part = p
         /\ wal[j].data = Token(wal[i].op, p) /\ wal[j].refs = {} /\ wal[j].first = 0

Init ==
  /\ wal = <<Marker("padding", 0)>>
  /\ durable = 1
  /\ archive = 0
  /\ source = [mode |-> "up", phase |-> "fragments", op |-> 1, part |-> 1,
                replay |-> 1, crashes |-> 0, ack |-> FALSE]
  /\ pins = {}
  /\ installed = {}
  /\ events = <<>>
  /\ scan = 1
  /\ offsets = [c \in Readers |-> 0]
  /\ history = [c \in Readers \ {1} |->
                   [mode |-> "ready", op |-> 0, staged |-> <<>>, results |-> <<>>]]
  /\ replica = [mode |-> "ready", op |-> 0, staged |-> <<>>, batch |-> <<>>, data |-> <<>>,
                 crashes |-> 0, pulse |-> 0, lastCrash |-> "none"]
  /\ witnesses = {}

AppendFragment ==
  /\ source.mode = "up" /\ source.phase = "fragments" /\ source.part <= ImageParts
  /\ wal' = Append(wal, Fragment(source.op, source.part))
  /\ source' = [source EXCEPT !.part = @ + 1]
  /\ pins' = IF source.part = 1 /\ Fault # "pin_late" THEN pins \cup {source.op} ELSE pins
  /\ UNCHANGED <<durable, archive, installed, events, scan, offsets, history, replica, witnesses>>

AppendManifest ==
  /\ source.mode = "up" /\ source.phase = "fragments"
  /\ source.part > ImageParts \/ (Fault = "publish_incomplete" /\ source.part = ImageParts)
  /\ wal' = Append(wal, Record("manifest", source.op, 0, <<>>, Refs(source.op), FirstPos(source.op)))
  /\ source' = [source EXCEPT !.phase = "advance"]
  /\ pins' = IF Fault = "pin_late" THEN pins \cup {source.op} ELSE pins
  /\ UNCHANGED <<durable, archive, installed, events, scan, offsets, history, replica, witnesses>>

BeginRollback ==
  /\ source.mode = "up" /\ source.phase = "advance" /\ source.op = RollbackAt
  /\ source' = [source EXCEPT !.phase = "rollback"]
  /\ UNCHANGED <<wal, durable, archive, pins, installed, events, scan, offsets, history, replica, witnesses>>

AppendCancel ==
  /\ source.mode = "up" /\ source.phase = "rollback"
  /\ wal' = Append(wal, Marker("cancel", source.op))
  /\ source' = [source EXCEPT !.phase = "cancelled"]
  /\ UNCHANGED <<durable, archive, pins, installed, events, scan, offsets, history, replica, witnesses>>

AdvanceOperation ==
  /\ source.mode = "up"
  /\ source.phase = "cancelled" \/ (source.phase = "advance" /\ source.op # RollbackAt)
  /\ source' = IF source.op = MaxOps THEN [source EXCEPT !.phase = "commit"]
                ELSE [source EXCEPT !.op = @ + 1, !.part = 1, !.phase = "fragments"]
  /\ UNCHANGED <<wal, durable, archive, pins, installed, events, scan, offsets, history, replica, witnesses>>

AppendCommit ==
  /\ source.mode = "up" /\ source.phase = "commit"
  /\ wal' = Append(wal, Marker("commit", 0))
  /\ source' = [source EXCEPT !.phase = "closing"]
  /\ UNCHANGED <<durable, archive, pins, installed, events, scan, offsets, history, replica, witnesses>>

FlushPrefix ==
  /\ source.mode = "up" /\ durable < Len(wal)
  /\ durable' = durable + 1
  /\ UNCHANGED <<wal, archive, source, pins, installed, events, scan, offsets, history, replica, witnesses>>

Acknowledge ==
  /\ source.mode = "up" /\ ~source.ack
  /\ DurableCommit \/ (Fault = "acknowledge_early" /\ Positions("commit", Len(wal)) # {})
  /\ source' = [source EXCEPT !.ack = TRUE, !.phase = "done"]
  /\ UNCHANGED <<wal, durable, archive, pins, installed, events, scan, offsets, history, replica, witnesses>>

InstallManifestMetadata ==
  /\ source.mode = "up" /\ HistoryNeeded
  /\ \E o \in Ops \ installed :
       /\ ManifestPos(o) > archive /\ ManifestPos(o) <= durable
       /\ installed' = installed \cup {o}
  /\ UNCHANGED <<wal, durable, archive, source, pins, events, scan, offsets, history, replica, witnesses>>

ReleaseConstructionPin ==
  /\ source.mode = "up"
  /\ \E o \in pins :
       /\ o \in installed \/ (Fault = "handoff_early" /\ ManifestPos(o) > 0)
       /\ pins' = pins \ {o}
  /\ UNCHANGED <<wal, durable, archive, source, installed, events, scan, offsets, history, replica, witnesses>>

DeleteArchivePrefix ==
  /\ source.mode = "up" \/ Fault = "restart_unprotected"
  /\ archive < durable
  /\ archive + 1 < RetentionFloor
  /\ archive' = archive + 1
  /\ witnesses' = witnesses
       \cup (IF source.phase = "fragments" /\ source.part > 1 THEN {"archive_during_construction"} ELSE {})
       \cup (IF source.crashes > 0 /\ source.mode = "up" /\ DurableCommit
                 /\ ~AllReadersDone /\ installed # {} THEN {"archive_after_restart"} ELSE {})
  /\ UNCHANGED <<wal, durable, source, pins, installed, events, scan, offsets, replica, history>>

SourceCrash ==
  /\ source.mode = "up" /\ source.crashes < SourceCrashes
  /\ ~DurableAbort /\ ~HistoryDrained
  /\ wal' = SubSeq(wal, 1, durable)
  /\ source' = [source EXCEPT !.mode = "down", !.phase = "recover", !.crashes = @ + 1]
  /\ pins' = {} /\ installed' = {}
  /\ UNCHANGED <<durable, archive, events, scan, offsets, history, replica, witnesses>>

BeginRecovery ==
  /\ source.mode = "down"
  /\ source' = [source EXCEPT !.mode = "replay", !.replay = archive + 1]
  /\ UNCHANGED <<wal, durable, archive, pins, installed, events, scan, offsets, history, replica, witnesses>>

ReplayRecord ==
  /\ source.mode = "replay" /\ source.replay <= durable
  /\ source' = [source EXCEPT !.replay = @ + 1]
  /\ pins' = IF wal[source.replay].kind = "fragment"
              THEN pins \cup {wal[source.replay].op} ELSE pins
  /\ installed' = IF wal[source.replay].kind = "manifest"
                   THEN installed \cup {wal[source.replay].op} ELSE installed
  /\ UNCHANGED <<wal, durable, archive, events, scan, offsets, history, replica, witnesses>>

FinishRecovery ==
  /\ source.mode = "replay" /\ source.replay > durable
  /\ source' = [source EXCEPT !.mode = "up",
                  !.phase = IF DurableCommit THEN IF source.ack THEN "done" ELSE "closing"
                             ELSE IF DurableAbort THEN "done" ELSE "abort"]
  /\ UNCHANGED <<wal, durable, archive, pins, installed, events, scan, offsets, history, replica, witnesses>>

AppendAbort ==
  /\ source.mode = "up" /\ source.phase = "abort"
  /\ wal' = Append(wal, Marker("abort", 0))
  /\ source' = [source EXCEPT !.phase = "aborting"]
  /\ UNCHANGED <<durable, archive, pins, installed, events, scan, offsets, history, replica, witnesses>>

FinishAbort ==
  /\ source.mode = "up" /\ source.phase = "aborting" /\ DurableAbort
  /\ source' = [source EXCEPT !.phase = "done"]
  /\ UNCHANGED <<wal, durable, archive, pins, installed, events, scan, offsets, history, replica, witnesses>>

RetireMetadata ==
  /\ source.mode = "up" /\ ~HistoryNeeded /\ (pins # {} \/ installed # {})
  /\ pins' = {} /\ installed' = {}
  /\ UNCHANGED <<wal, durable, archive, source, events, scan, offsets, history, replica, witnesses>>

ExposeEvent ==
  /\ DurableCommit /\ scan <= MaxOps
  /\ events' = IF scan \in AvailableCancels /\ Fault # "ignore_cancel" THEN events ELSE Append(events, scan)
  /\ scan' = scan + 1
  /\ UNCHANGED <<wal, durable, archive, source, pins, installed, offsets, history, replica, witnesses>>

BeginHistoryRead(c) ==
  /\ c \in Readers \ {1} /\ history[c].mode = "ready" /\ offsets[c] < Len(events)
  /\ history' = [history EXCEPT ![c].mode = "reading", ![c].op = events[offsets[c] + 1], ![c].staged = <<>>]
  /\ UNCHANGED <<wal, durable, archive, source, pins, installed, events, scan, offsets, replica, witnesses>>

ReadHistoryFragment(c) ==
  /\ c \in Readers \ {1} /\ history[c].mode = "reading" /\ Len(history[c].staged) < ReadParts
  /\ LET pos == FragmentPos(history[c].op, DecodePart(Len(history[c].staged) + 1))
     IN /\ pos > archive /\ pos <= durable
        /\ history' = [history EXCEPT ![c].staged = Append(@, wal[pos].data)]
  /\ UNCHANGED <<wal, durable, archive, source, pins, installed, events, scan, offsets, replica, witnesses>>

ReadHistory(c) ==
  /\ c \in Readers \ {1} /\ history[c].mode = "reading" /\ Len(history[c].staged) = ReadParts
  /\ history' = [history EXCEPT ![c].mode = "ready", ![c].staged = <<>>,
                   ![c].results = Append(@, [op |-> history[c].op, image |-> history[c].staged])]
  /\ offsets' = [offsets EXCEPT ![c] = @ + 1]
  /\ UNCHANGED <<wal, durable, archive, source, pins, installed, events, scan, replica, witnesses>>

BeginReplica ==
  /\ replica.mode = "ready" /\ scan > MaxOps /\ offsets[1] < Len(events)
  /\ replica' = [replica EXCEPT !.mode = "staging", !.op = events[offsets[1] + 1],
                  !.staged = <<>>, !.batch = <<>>]
  /\ UNCHANGED <<wal, durable, archive, source, pins, installed, events, scan, offsets, witnesses, history>>

DecodeFragment ==
  /\ replica.mode = "staging" /\ Len(replica.staged) < ReadParts
  /\ LET pos == FragmentPos(replica.op, DecodePart(Len(replica.staged) + 1))
     IN /\ pos > archive /\ pos <= durable
        /\ replica' = IF Fault = "decoder_livelock"
                       THEN [replica EXCEPT !.pulse = 1 - @]
                       ELSE [replica EXCEPT !.staged = Append(@, wal[pos].data)]
  /\ UNCHANGED <<wal, durable, archive, source, pins, installed, events, scan, offsets, witnesses, history>>

StageOperation ==
  /\ replica.mode = "staging" /\ Len(replica.staged) = ReadParts
  /\ LET next == offsets[1] + Len(replica.batch) + 2
         last == next > Len(events) \/ Fault = "partial_transaction_commit"
     IN replica' = [replica EXCEPT !.batch = Append(@, [op |-> replica.op, image |-> replica.staged]),
                      !.mode = IF last THEN "batch_ready" ELSE "staging",
                      !.op = IF last THEN @ ELSE events[next], !.staged = <<>>]
  /\ UNCHANGED <<wal, durable, archive, source, pins, installed, events, scan, offsets, witnesses, history>>

CommitReplica ==
  /\ replica.mode = "batch_ready"
  /\ replica' = [replica EXCEPT !.data = @ \o replica.batch,
                  !.mode = IF Fault = "split_replica_commit" THEN "data_committed" ELSE "ready",
                  !.batch = IF Fault = "split_replica_commit" THEN @ ELSE <<>>]
  /\ offsets' = IF Fault = "split_replica_commit" THEN offsets
                 ELSE [offsets EXCEPT ![1] = @ + Len(replica.batch)]
  /\ UNCHANGED <<wal, durable, archive, source, pins, installed, events, scan, witnesses, history>>

SaveReplicaProgress ==
  /\ replica.mode = "data_committed"
  /\ replica' = [replica EXCEPT !.mode = "ready", !.batch = <<>>]
  /\ offsets' = [offsets EXCEPT ![1] = @ + Len(replica.batch)]
  /\ UNCHANGED <<wal, durable, archive, source, pins, installed, events, scan, witnesses, history>>

ReplicaCrash ==
  /\ replica.mode # "down" /\ replica.crashes < ReplicaCrashes
  /\ (replica.mode \in {"staging", "batch_ready"} \/ Len(replica.data) > 0)
  /\ replica' = [replica EXCEPT !.mode = "down", !.staged = <<>>, !.batch = <<>>, !.crashes = @ + 1,
                  !.lastCrash = IF Len(replica.data) > 0 THEN "after"
                                ELSE IF replica.mode = "batch_ready" THEN "before" ELSE "partial"]
  /\ offsets' = IF Fault = "forget_replica_progress" THEN [offsets EXCEPT ![1] = 0] ELSE offsets
  /\ UNCHANGED <<wal, durable, archive, source, pins, installed, events, scan, witnesses, history>>

RestartReplica ==
  /\ replica.mode = "down"
  /\ replica' = [replica EXCEPT !.mode = "ready"]
  /\ UNCHANGED <<wal, durable, archive, source, pins, installed, events, scan, offsets, witnesses, history>>

Complete ==
  /\ source.mode = "up" /\ source.phase = "done" /\ durable = Len(wal)
  /\ DurableAbort \/ (HistoryDrained /\ Len(events) = Len(ExpectedEvents))
  /\ replica.mode = "ready" /\ pins = {} /\ installed = {}

Done == Complete /\ UNCHANGED vars

Next == AppendFragment \/ AppendManifest \/ BeginRollback \/ AppendCancel \/ AdvanceOperation \/ AppendCommit
        \/ FlushPrefix \/ Acknowledge \/ InstallManifestMetadata \/ ReleaseConstructionPin
        \/ DeleteArchivePrefix \/ SourceCrash \/ BeginRecovery \/ ReplayRecord \/ FinishRecovery
        \/ AppendAbort \/ FinishAbort \/ RetireMetadata \/ ExposeEvent
        \/ (\E c \in Readers : BeginHistoryRead(c) \/ ReadHistoryFragment(c) \/ ReadHistory(c))
        \/ BeginReplica \/ DecodeFragment \/ StageOperation \/ CommitReplica \/ SaveReplicaProgress
        \/ ReplicaCrash \/ RestartReplica \/ Done

Fairness ==
  /\ WF_vars(AppendFragment) /\ WF_vars(AppendManifest) /\ WF_vars(BeginRollback) /\ WF_vars(AppendCancel)
  /\ WF_vars(AdvanceOperation) /\ WF_vars(AppendCommit) /\ WF_vars(FlushPrefix)
  /\ WF_vars(Acknowledge) /\ WF_vars(BeginRecovery) /\ WF_vars(ReplayRecord)
  /\ WF_vars(FinishRecovery) /\ WF_vars(AppendAbort) /\ WF_vars(FinishAbort)
  /\ WF_vars(ExposeEvent) /\ WF_vars(BeginReplica) /\ WF_vars(DecodeFragment)
  /\ WF_vars(StageOperation) /\ WF_vars(CommitReplica) /\ WF_vars(SaveReplicaProgress) /\ WF_vars(RestartReplica)
  /\ \A c \in Readers : WF_vars(BeginHistoryRead(c)) /\ WF_vars(ReadHistoryFragment(c)) /\ WF_vars(ReadHistory(c))

Spec == Init /\ [][Next]_vars /\ Fairness

TypeOK ==
  /\ Len(wal) <= MaxRecords /\ durable \in 0..Len(wal) /\ archive \in 0..durable
  /\ \A i \in 1..Len(wal) :
       /\ wal[i].kind \in Kinds /\ wal[i].op \in 0..MaxOps /\ wal[i].part \in 0..ImageParts
       /\ wal[i].first \in 0..MaxRecords /\ wal[i].refs \subseteq (Ops \X (1..ImageParts))
       /\ wal[i].data \in Tokens \cup {<<>>}
  /\ source.mode \in {"up", "down", "replay"}
  /\ source.phase \in {"fragments", "advance", "rollback", "cancelled", "commit", "closing", "recover", "abort", "aborting", "done"}
  /\ source.op \in Ops /\ source.part \in 1..(ImageParts + 1)
  /\ source.replay \in 1..(MaxRecords + 1) /\ source.crashes \in 0..SourceCrashes
  /\ source.ack \in BOOLEAN /\ pins \subseteq Ops /\ installed \subseteq Ops
  /\ events \in Seq(Ops) /\ Len(events) <= MaxOps /\ scan \in 1..(MaxOps + 1)
  /\ offsets \in [Readers -> 0..MaxOps]
  /\ history \in [Readers \ {1} -> [mode : {"ready", "reading"}, op : 0..MaxOps,
                                         staged : Seq(Tokens), results : Seq([op : Ops, image : Seq(Tokens)])]]
  /\ \A c \in Readers \ {1} :
       /\ Len(history[c].staged) <= ImageParts /\ Len(history[c].results) <= MaxOps
       /\ \A i \in 1..Len(history[c].results) : Len(history[c].results[i].image) <= ImageParts
  /\ replica.mode \in {"ready", "staging", "batch_ready", "data_committed", "down"}
  /\ replica.op \in 0..MaxOps /\ replica.staged \in Seq(Tokens) /\ Len(replica.staged) <= ImageParts
  /\ replica.data \in Seq([op : Ops, image : Seq(Tokens)])
  /\ \A i \in 1..Len(replica.data) : Len(replica.data[i].image) <= ImageParts
  /\ replica.batch \in Seq([op : Ops, image : Seq(Tokens)]) /\ Len(replica.batch) <= MaxOps
  /\ \A i \in 1..Len(replica.batch) : Len(replica.batch[i].image) <= ImageParts
  /\ Len(replica.data) <= MaxOps * (ReplicaCrashes + 1)
  /\ replica.crashes \in 0..ReplicaCrashes /\ replica.pulse \in 0..1
  /\ replica.lastCrash \in {"none", "partial", "before", "after"}
  /\ witnesses \subseteq {"archive_during_construction", "archive_after_restart"}

HistoryImagesComplete ==
  /\ \A i \in Positions("manifest", Len(wal)) : ManifestComplete(i)
  /\ Len(events) <= Len(ExpectedEvents)
  /\ \A i \in 1..Len(events) : events[i] = ExpectedEvents[i]

DurableAcknowledgement == source.ack => DurableCommit
RetainedDependencies == \A i \in RequiredPositions : i > archive
CancellationMetadataRetained == HistoryNeeded => \A i \in Positions("cancel", durable) : i > archive
ReplicaCommittedPrefix ==
  /\ Len(replica.data) = offsets[1]
  /\ Len(replica.data) <= Len(ExpectedEvents)
  /\ \A i \in 1..Len(replica.data) :
       /\ replica.data[i].op = ExpectedEvents[i]
       /\ replica.data[i].image = Image(ExpectedEvents[i])
ReaderResults(c) == IF c = 1 THEN replica.data ELSE history[c].results
HistoryReadExact ==
  \A c \in Readers :
    /\ Len(ReaderResults(c)) = offsets[c]
    /\ Len(ReaderResults(c)) <= Len(ExpectedEvents)
    /\ \A i \in 1..Len(ReaderResults(c)) :
         /\ ReaderResults(c)[i].op = ExpectedEvents[i]
         /\ ReaderResults(c)[i].image = Image(ExpectedEvents[i])
ReplicaAtMostOnce == \A o \in Ops : Cardinality({i \in 1..Len(replica.data) : replica.data[i].op = o}) <= 1
ReplicaTransactionAtomic == Len(replica.data) = 0 \/ Len(replica.data) = Len(ExpectedEvents)
EventuallyDrained == (DurableCommit /\ Len(events) > 0) ~> AllReadersDone

NoDrainAntecedent == ~(DurableCommit /\ Len(events) > 0)
NoRollbackCommit == ~(RollbackAt > 0 /\ RollbackAt \in DurableCancels /\ DurableCommit /\ Len(events) > 0)
NoMultipleFragments == ~(Parts > 1 /\ Len(replica.staged) = ImageParts)
NoArchiveDuringConstruction == "archive_during_construction" \notin witnesses
NoArchiveAfterRestart == "archive_after_restart" \notin witnesses
NoReplicaCrashBeforeCommit == replica.lastCrash # "before"
NoReplicaCrashAfterCommit == replica.lastCrash # "after"
NoHistoryReaderResult == ~ (\E c \in Readers \ {1} :
                              Len(history[c].results) > 0 /\ history[c].results[1].image = Image(ExpectedEvents[1]))
NoTwoOperationReplicaCommit == ~(Len(ExpectedEvents) >= 2 /\ Len(replica.data) = Len(ExpectedEvents))

=============================================================================
