------------------------ MODULE SidecarBoundary ------------------------
EXTENDS Naturals, Sequences, FiniteSets, TLC

CONSTANTS MaxOps, Parts, SourceCrashes, ReplicaCrashes, Fault
Ops == 1..MaxOps
ImageParts == 2 * Parts
MaxRecords == 2 + MaxOps * (ImageParts + 1) + 2

VARIABLES wal, durable, archive, source, pins, installed, op, current,
          parent, saved, oldLive, snapshot, replica, violations
vars == <<wal, durable, archive, source, pins, installed, op, current,
          parent, saved, oldLive, snapshot, replica, violations>>

History == INSTANCE SidecarHistory WITH
  MaxOps <- MaxOps, Parts <- Parts, RollbackAt <- 2,
  SourceCrashes <- SourceCrashes, ReplicaCrashes <- ReplicaCrashes,
  Consumers <- 1, Fault <- IF Fault = "writer_swapped" THEN "writer_swapped" ELSE "none",
  wal <- wal, durable <- durable,
  archive <- archive, source <- source, pins <- pins, installed <- installed,
  events <- <<>>, scan <- MaxOps + 1, offsets <- [c \in 1..1 |-> replica.progress],
  history <- [c \in {} |-> [mode |-> "ready", op |-> 0, staged |-> <<>>, results |-> <<>>]],
  replica <- replica, witnesses <- {}

EmptyOperation == [chunks |-> 0, published |-> FALSE, undoing |-> FALSE,
                   finished |-> FALSE, cancelled |-> FALSE,
                   restored |-> 0]
Init ==
  /\ wal = <<History!Marker("padding", 0)>> /\ durable = 1 /\ archive = 0
  /\ source = [mode |-> "up", phase |-> "active", crashes |-> 0, replay |-> 1,
                ack |-> FALSE, lastRecovery |-> "none"]
  /\ pins = {} /\ installed = {}
  /\ op = [o \in Ops |-> EmptyOperation]
  /\ current = 1 /\ parent = 0 /\ saved = 0
  /\ oldLive = TRUE /\ snapshot = TRUE
  /\ replica = [mode |-> "ready", target |-> 0, staged |-> <<>>,
                 batch |-> <<>>, data |-> <<>>, progress |-> 0, crashes |-> 0,
                 crashKind |-> "none", pulse |-> 0]
  /\ violations = [gcRecovery |-> FALSE, archiveRecovery |-> FALSE]

ImageReady(o) == History!ManifestPos(o) > 0 /\ History!ManifestComplete(History!ManifestPos(o))
LocallyCancelled == {o \in Ops : op[o].cancelled}
Active == source.mode = "up" /\ source.phase = "active" /\ current \in Ops

InsertChunk ==
  /\ Active /\ ~op[current].published /\ ~op[current].undoing
  /\ op[current].chunks < 2
  /\ op' = [op EXCEPT ![current].chunks = @ + 1]
  /\ UNCHANGED <<wal, durable, archive, source, pins, installed, current, parent,
                 saved, oldLive, snapshot, replica, violations>>

AppendFragment ==
  /\ Active /\ ~op[current].published /\ ~op[current].undoing
  /\ History!ManifestPos(current) = 0
  /\ LET n == Cardinality({i \in 1..Len(wal) : wal[i].kind = "fragment" /\ wal[i].op = current})
     IN /\ n < ImageParts
        /\ wal' = Append(wal, History!Fragment(current, n + 1))
        /\ pins' = IF n = 0 /\ Fault # "late_pin" THEN pins \cup {current} ELSE pins
  /\ UNCHANGED <<durable, archive, source, installed, op, current, parent,
                 saved, oldLive, snapshot, replica, violations>>

AppendManifest ==
  /\ Active /\ ~op[current].published /\ ~op[current].undoing
  /\ History!ManifestPos(current) = 0
  /\ LET n == Cardinality({i \in 1..Len(wal) : wal[i].kind = "fragment" /\ wal[i].op = current})
     IN /\ n = ImageParts \/ (Fault = "missing_fragment" /\ n = ImageParts - 1)
        /\ wal' = Append(wal, History!Record("manifest", current, 0, <<>>,
                          History!Refs(current), History!FirstPos(current)))
        /\ pins' = IF Fault = "late_pin" THEN pins \cup {current} ELSE pins
  /\ UNCHANGED <<durable, archive, source, installed, op, current, parent,
                 saved, oldLive, snapshot, replica, violations>>

Publish ==
  /\ Active /\ ~op[current].published /\ ~op[current].undoing
  /\ (op[current].chunks = 2 /\ ImageReady(current)) \/ Fault = "early_publish"
  /\ parent' = current
  /\ op' = [op EXCEPT ![current].published = TRUE]
  /\ UNCHANGED <<wal, durable, archive, source, pins, installed, current,
                 saved, oldLive, snapshot, replica, violations>>

FinishOperation ==
  /\ Active /\ current # 2 /\ op[current].published
  /\ op' = [op EXCEPT ![current].finished = TRUE]
  /\ saved' = IF current = 1 THEN parent ELSE saved
  /\ current' = current + 1
  /\ UNCHANGED <<wal, durable, archive, source, pins, installed, parent,
                 oldLive, snapshot, replica, violations>>

StartRollback ==
  /\ Active /\ current = 2 /\ op[2].published /\ ~op[2].undoing
  /\ op' = [op EXCEPT ![2].undoing = TRUE]
  /\ UNCHANGED <<wal, durable, archive, source, pins, installed, current, parent,
                 saved, oldLive, snapshot, replica, violations>>

NotifyCancellation ==
  /\ Active /\ current = 2 /\ op[2].undoing
  /\ ~\E i \in 1..Len(wal) : wal[i].kind = "cancel" /\ wal[i].op = 2
  /\ wal' = Append(wal, History!Marker("cancel", 2))
  /\ UNCHANGED <<durable, archive, source, pins, installed, op, current, parent,
                 saved, oldLive, snapshot, replica, violations>>

UndoParent ==
  /\ Active /\ current = 2 /\ op[2].undoing /\ op[2].published
  /\ parent' = IF Fault = "bad_restore" THEN 0 ELSE saved
  /\ op' = [op EXCEPT ![2].published = FALSE]
  /\ UNCHANGED <<wal, durable, archive, source, pins, installed, current,
                 saved, oldLive, snapshot, replica, violations>>

UndoLogicalChunk ==
  /\ Active /\ current = 2 /\ op[2].undoing /\ ~op[2].published /\ op[2].chunks > 0
  /\ op' = [op EXCEPT ![2].chunks = @ - 1]
  /\ UNCHANGED <<wal, durable, archive, source, pins, installed, current, parent,
                 saved, oldLive, snapshot, replica, violations>>

FinishRollback ==
  /\ Active /\ current = 2 /\ op[2].undoing /\ ~op[2].published
  /\ op[2].chunks = 0 \/ Fault = "early_finish_rollback"
  /\ (\E i \in 1..Len(wal) : wal[i].kind = "cancel" /\ wal[i].op = 2) \/ Fault = "drop_cancel"
  /\ op' = [op EXCEPT ![2].undoing = FALSE, ![2].finished = TRUE,
                       ![2].cancelled = TRUE, ![2].restored = parent]
  /\ current' = 3
  /\ UNCHANGED <<wal, durable, archive, source, pins, installed, parent,
                 saved, oldLive, snapshot, replica, violations>>

AppendCommit ==
  /\ source.mode = "up" /\ source.phase = "active"
  /\ current = MaxOps + 1 \/ (Fault = "early_commit" /\ current > 1)
  /\ wal' = Append(wal, History!Marker("commit", 0))
  /\ source' = [source EXCEPT !.phase = "closing"]
  /\ UNCHANGED <<durable, archive, pins, installed, op, current, parent,
                 saved, oldLive, snapshot, replica, violations>>

Flush ==
  /\ source.mode = "up" /\ durable < Len(wal)
  /\ durable' = durable + 1
  /\ UNCHANGED <<wal, archive, source, pins, installed, op, current, parent,
                 saved, oldLive, snapshot, replica, violations>>

Acknowledge ==
  /\ source.mode = "up" /\ source.phase = "closing" /\ ~source.ack
  /\ History!DurableCommit \/ Fault = "early_ack"
  /\ source' = [source EXCEPT !.ack = TRUE]
  /\ UNCHANGED <<wal, durable, archive, pins, installed, op, current, parent,
                 saved, oldLive, snapshot, replica, violations>>

InstallMetadata ==
  /\ source.mode = "up" /\ source.phase # "aborted"
  /\ \E o \in Ops \ installed :
       /\ History!ManifestPos(o) > archive /\ History!ManifestPos(o) <= durable
       /\ installed' = installed \cup {o}
  /\ UNCHANGED <<wal, durable, archive, source, pins, op, current, parent,
                 saved, oldLive, snapshot, replica, violations>>

ReleasePin ==
  /\ source.mode = "up" /\ \E o \in pins \cap installed : pins' = pins \ {o}
  /\ UNCHANGED <<wal, durable, archive, source, installed, op, current, parent,
                 saved, oldLive, snapshot, replica, violations>>

ReleaseDependencies ==
  /\ source.mode = "up" /\ (source.phase = "aborted" \/
      (History!DurableCommit /\ (replica.progress = Len(History!ExpectedEvents) \/ Fault = "drop_cancel_retention")))
  /\ pins # {} \/ installed # {}
  /\ pins' = {} /\ installed' = {}
  /\ UNCHANGED <<wal, durable, archive, source, op, current, parent,
                 saved, oldLive, snapshot, replica, violations>>

DeleteArchive ==
  /\ source.mode = "up" \/ Fault = "early_archive"
  /\ archive < durable /\ archive + 1 < History!RetentionFloor
  /\ archive' = archive + 1
  /\ violations' = [violations EXCEPT !.archiveRecovery = @ \/ source.mode # "up"]
  /\ UNCHANGED <<wal, durable, source, pins, installed, op, current, parent,
                 saved, oldLive, snapshot, replica>>

CloseSnapshot ==
  /\ snapshot /\ snapshot' = FALSE
  /\ UNCHANGED <<wal, durable, archive, source, pins, installed, op, current,
                 parent, saved, oldLive, replica, violations>>

CollectOld ==
  /\ oldLive /\ History!DurableCommit
  /\ source.mode = "up" \/ Fault = "early_gc"
  /\ ~snapshot \/ Fault = "ignore_snapshot"
  /\ oldLive' = FALSE
  /\ violations' = [violations EXCEPT !.gcRecovery = @ \/ source.mode # "up"]
  /\ UNCHANGED <<wal, durable, archive, source, pins, installed, op, current,
                 parent, saved, snapshot, replica>>

Complete ==
  /\ source.mode = "up" /\ ~snapshot /\ pins = {} /\ installed = {}
  /\ replica.mode = "ready"
  /\ source.phase = "aborted" \/ (source.phase = "closing" /\ source.ack /\
       History!DurableCommit /\ replica.progress = Len(History!ExpectedEvents) /\ ~oldLive)

SourceCrash ==
  /\ source.crashes < SourceCrashes /\ ~Complete
  /\ wal' = SubSeq(wal, 1, durable)
  /\ source' = [source EXCEPT !.mode = "recovery", !.crashes = @ + 1, !.replay = archive + 1]
  /\ pins' = {} /\ installed' = {}
  /\ UNCHANGED <<durable, archive, op, current, parent, saved, oldLive, snapshot,
                 replica, violations>>

Replay ==
  /\ source.mode = "recovery" /\ source.replay > archive /\ source.replay <= durable
  /\ LET r == wal[source.replay] IN
     /\ pins' = IF r.kind = "fragment" THEN pins \cup {r.op} ELSE pins
     /\ installed' = IF r.kind = "manifest" THEN installed \cup {r.op} ELSE installed
  /\ source' = [source EXCEPT !.replay = @ + 1]
  /\ UNCHANGED <<wal, durable, archive, op, current, parent, saved, oldLive,
                 snapshot, replica, violations>>

RecoverParent ==
  /\ source.mode = "recovery" /\ ~History!DurableCommit /\ parent # 0
  /\ parent' = 0
  /\ UNCHANGED <<wal, durable, archive, source, pins, installed, op, current,
                 saved, oldLive, snapshot, replica, violations>>

RecoverChunk ==
  /\ source.mode = "recovery" /\ ~History!DurableCommit
  /\ \E o \in Ops : /\ op[o].chunks > 0
                    /\ op' = [op EXCEPT ![o].chunks = @ - 1]
  /\ UNCHANGED <<wal, durable, archive, source, pins, installed, current,
                 parent, saved, oldLive, snapshot, replica, violations>>

FinishRecovery ==
  /\ source.mode = "recovery" /\ source.replay > durable
  /\ History!DurableCommit \/ (parent = 0 /\ \A o \in Ops : op[o].chunks = 0)
     \/ Fault = "early_abort_recovery"
  /\ source' = [source EXCEPT !.mode = "up",
       !.phase = IF History!DurableCommit THEN "closing" ELSE "aborted",
       !.lastRecovery = IF History!DurableCommit THEN "commit" ELSE "abort"]
  /\ UNCHANGED <<wal, durable, archive, pins, installed, op, current, parent,
                 saved, oldLive, snapshot, replica, violations>>

BeginReplica ==
  /\ source.mode = "up" /\ History!DurableCommit
  /\ replica.mode = "ready" /\ replica.progress < Len(History!ExpectedEvents)
  /\ replica' = [replica EXCEPT !.mode = "staging",
       !.target = History!ExpectedEvents[replica.progress + 1], !.staged = <<>>, !.batch = <<>>]
  /\ UNCHANGED <<wal, durable, archive, source, pins, installed, op, current,
                 parent, saved, oldLive, snapshot, violations>>

StageFragment ==
  /\ replica.mode = "staging" /\ Len(replica.staged) < ImageParts
  /\ LET part == Len(replica.staged) + 1
         selected == IF Fault = "decode_swap"
                     THEN IF part <= Parts THEN part + Parts ELSE part - Parts
                     ELSE part
         pos == History!FragmentPos(replica.target, selected)
     IN /\ pos > archive /\ pos <= durable
        /\ replica' = IF Fault = "livelock"
                       THEN [replica EXCEPT !.pulse = 1 - @]
                       ELSE [replica EXCEPT !.staged = Append(@, wal[pos].data)]
  /\ UNCHANGED <<wal, durable, archive, source, pins, installed, op, current,
                 parent, saved, oldLive, snapshot, violations>>

FinishReplicaEvent ==
  /\ replica.mode = "staging" /\ Len(replica.staged) = ImageParts
  /\ LET batch == Append(replica.batch, [op |-> replica.target, image |-> replica.staged])
         next == replica.progress + Len(batch) + 1
         ready == next > Len(History!ExpectedEvents) \/ Fault = "partial_transaction"
     IN /\ replica' = [replica EXCEPT !.batch = batch, !.staged = <<>>,
          !.mode = IF ready THEN "batchReady" ELSE "staging",
          !.target = IF ready THEN @ ELSE History!ExpectedEvents[next]]
  /\ UNCHANGED <<wal, durable, archive, source, pins, installed, op, current,
                 parent, saved, oldLive, snapshot, violations>>

CommitReplica ==
  /\ replica.mode = "batchReady" /\ Len(replica.batch) > 0
  /\ replica' = [replica EXCEPT
       !.data = @ \o replica.batch,
       !.progress = IF Fault = "split_replica" THEN @ ELSE @ + Len(replica.batch),
       !.mode = IF Fault = "split_replica" THEN "dataOnly" ELSE "ready",
       !.batch = <<>>, !.staged = <<>>]
  /\ UNCHANGED <<wal, durable, archive, source, pins, installed, op, current,
                 parent, saved, oldLive, snapshot, violations>>

SaveProgress ==
  /\ replica.mode = "dataOnly"
  /\ replica' = [replica EXCEPT !.mode = "ready", !.progress = Len(replica.data)]
  /\ UNCHANGED <<wal, durable, archive, source, pins, installed, op, current,
                 parent, saved, oldLive, snapshot, violations>>

ReplicaCrash ==
  /\ replica.crashes < ReplicaCrashes /\ replica.mode # "down" /\ ~Complete
  /\ replica.mode \in {"staging", "batchReady", "dataOnly"} \/ Len(replica.data) > 0
  /\ replica' = [replica EXCEPT !.mode = "down", !.staged = <<>>, !.batch = <<>>,
       !.crashes = @ + 1, !.progress = IF Fault = "lose_progress" THEN 0 ELSE @,
       !.crashKind = IF Len(replica.data) = 0 THEN "before" ELSE "after"]
  /\ UNCHANGED <<wal, durable, archive, source, pins, installed, op, current,
                 parent, saved, oldLive, snapshot, violations>>

RestartReplica ==
  /\ replica.mode = "down"
  /\ replica' = [replica EXCEPT !.mode = "ready", !.target = 0]
  /\ UNCHANGED <<wal, durable, archive, source, pins, installed, op, current,
                 parent, saved, oldLive, snapshot, violations>>

Done == Complete /\ UNCHANGED vars
Next == InsertChunk \/ AppendFragment \/ AppendManifest \/ Publish \/ FinishOperation
        \/ StartRollback \/ NotifyCancellation \/ UndoParent \/ UndoLogicalChunk \/ FinishRollback
        \/ AppendCommit \/ Flush \/ Acknowledge \/ InstallMetadata \/ ReleasePin
        \/ ReleaseDependencies \/ DeleteArchive \/ CloseSnapshot \/ CollectOld
        \/ SourceCrash \/ Replay \/ RecoverParent \/ RecoverChunk \/ FinishRecovery
        \/ BeginReplica \/ StageFragment \/ FinishReplicaEvent \/ CommitReplica \/ SaveProgress
        \/ ReplicaCrash \/ RestartReplica \/ Done
Fairness ==
  /\ WF_vars(InsertChunk) /\ WF_vars(AppendFragment) /\ WF_vars(AppendManifest)
  /\ WF_vars(Publish) /\ WF_vars(FinishOperation) /\ WF_vars(StartRollback)
  /\ WF_vars(NotifyCancellation) /\ WF_vars(UndoParent) /\ WF_vars(UndoLogicalChunk)
  /\ WF_vars(FinishRollback) /\ WF_vars(AppendCommit) /\ WF_vars(Flush)
  /\ WF_vars(Acknowledge) /\ WF_vars(Replay) /\ WF_vars(RecoverParent)
  /\ WF_vars(RecoverChunk) /\ WF_vars(FinishRecovery) /\ WF_vars(BeginReplica)
  /\ WF_vars(StageFragment) /\ WF_vars(FinishReplicaEvent) /\ WF_vars(CommitReplica) /\ WF_vars(SaveProgress)
  /\ WF_vars(RestartReplica) /\ WF_vars(CloseSnapshot) /\ WF_vars(CollectOld)
  /\ WF_vars(ReleaseDependencies)
Spec == Init /\ [][Next]_vars /\ Fairness

TypeOK ==
  /\ durable \in 0..Len(wal) /\ archive \in 0..durable /\ Len(wal) <= MaxRecords
  /\ current \in 1..(MaxOps + 1) /\ parent \in 0..MaxOps /\ saved \in 0..MaxOps
  /\ \A o \in Ops : /\ op[o].chunks \in 0..2 /\ op[o].restored \in 0..MaxOps
       /\ op[o].published \in BOOLEAN /\ op[o].undoing \in BOOLEAN
       /\ op[o].finished \in BOOLEAN /\ op[o].cancelled \in BOOLEAN
  /\ source.mode \in {"up", "recovery"} /\ source.phase \in {"active", "closing", "aborted"}
  /\ source.crashes \in 0..SourceCrashes /\ source.replay \in 1..(MaxRecords + 1)
  /\ source.ack \in BOOLEAN /\ source.lastRecovery \in {"none", "abort", "commit"}
  /\ pins \subseteq Ops /\ installed \subseteq Ops /\ snapshot \in BOOLEAN /\ oldLive \in BOOLEAN
  /\ replica.mode \in {"ready", "staging", "batchReady", "dataOnly", "down"}
  /\ replica.target \in 0..MaxOps /\ replica.progress \in 0..MaxOps
  /\ Len(replica.data) <= MaxOps + 1 /\ Len(replica.staged) <= ImageParts
  /\ Len(replica.batch) <= MaxOps
  /\ replica.crashes \in 0..ReplicaCrashes /\ replica.crashKind \in {"none", "before", "after"}
  /\ replica.pulse \in 0..1 /\ violations.gcRecovery \in BOOLEAN /\ violations.archiveRecovery \in BOOLEAN

PublishedReady == source.mode = "up" /\ source.phase # "aborted" /\ parent > 0 =>
                  op[parent].chunks = 2 /\ ImageReady(parent)
HistoryImagesComplete == \A i \in History!Positions("manifest", Len(wal)) : History!ManifestComplete(i)
SavepointPrefix == op[2].cancelled => op[2].restored = saved /\ saved = 1
CancellationChunksRestored == op[2].cancelled => op[2].chunks = 0
AbortRestored == source.mode = "up" /\ source.phase = "aborted" =>
  parent = 0 /\ \A o \in Ops : op[o].chunks = 0
CancellationAgreement == History!DurableCommit => LocallyCancelled = History!DurableCancels
CommitReady == History!DurableCommit =>
  /\ parent = MaxOps
  /\ \A o \in Ops \ LocallyCancelled : op[o].finished /\ op[o].chunks = 2 /\ ImageReady(o)
DurableAcknowledgement == History!DurableAcknowledgement
RetainedDependencies == source.phase # "aborted" => History!RetainedDependencies
CancellationMetadataRetained == source.phase # "aborted" => History!CancellationMetadataRetained
GCRecoverySafe == ~violations.gcRecovery
ArchiveRecoverySafe == ~violations.archiveRecovery
SnapshotSafe == snapshot => oldLive
ReplicaCommittedPrefix == History!ReplicaCommittedPrefix
HistoryReadExact == History!HistoryReadExact
ReplicaAtMostOnce == History!ReplicaAtMostOnce
ReplicaTransactionAtomic == History!ReplicaTransactionAtomic
EventuallyApplied == (History!DurableCommit /\ replica.progress < Len(History!ExpectedEvents))
                    ~> (replica.progress = Len(History!ExpectedEvents))

NoPendingWitness == ~(History!DurableCommit /\ replica.progress < Len(History!ExpectedEvents))
NoCancelCommitWitness == ~(History!DurableCommit /\ op[2].cancelled /\ Len(History!ExpectedEvents) > 0)
NoAbortRecoveryWitness == ~(source.lastRecovery = "abort")
NoPublishedAbortRecoveryWitness == ~(source.mode = "up" /\ source.phase = "aborted" /\
  source.lastRecovery = "abort" /\ op[1].published /\ parent = 0 /\
  \A o \in Ops : op[o].chunks = 0)
NoCancelRestoredWitness == ~(op[2].cancelled /\ op[2].chunks = 0 /\
  op[2].restored = saved /\ saved = 1)
NoCommitRecoveryWitness == ~(source.lastRecovery = "commit" /\ pins # {})
NoFirstPinWitness == ~(History!FirstPos(1) > 0 /\ History!ManifestPos(1) = 0 /\ 1 \in pins)
NoSnapshotWitness == ~(History!DurableCommit /\ snapshot /\ oldLive)
NoReplicaRetryWitness == ~(replica.crashKind = "before" /\ Len(replica.data) > 0)
NoReplicaAfterWitness == ~(replica.crashKind = "after" /\ replica.mode = "ready" /\ replica.progress > 0)
NoStagedWitness == ~(replica.mode = "staging" /\ Len(replica.staged) = ImageParts)
NoBatchWitness == ~(replica.mode = "batchReady" /\ Len(replica.batch) >= 2)
NoBatchCommitWitness == ~(Len(replica.data) >= 2 /\ replica.progress = Len(replica.data))
=============================================================================
