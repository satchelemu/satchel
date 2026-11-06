// Synthetic journals only. Appended to the production WarmupJournal and WarmupResume by run.py.
var checks = 0
func check(_ condition: @autoclosure () -> Bool, _ message: String) {
    precondition(condition(), message); checks += 1
}
func decode(_ json: String) -> WarmupJournal {
    guard let value = try? JSONDecoder().decode(WarmupJournal.self, from: Data(json.utf8)) else { fatalError("Undecodable: " + json) }
    return value
}
func progress(_ state: String, _ stage: String = "Synthetic shader work") -> WarmupProgress {
    var value = WarmupProgress(stage: stage, completed: 3, total: 9); value.state = state; return value
}
let stoppedStage = "Stopped by iOS while Satchel was in the background. It resumes when you return."

// A journal written before the field existed, by a warm-up Prepare started and iOS stopped:
// it stays paused, keeps its counts, and its words no longer promise an automatic resume.
let legacy = WarmupResume.restored(decode("""
{"package":"Synthetic-1","title":"Synthetic game","interrupted":true,
 "progress":{"stage":"\(stoppedStage)","completed":3,"total":9,"shaders":2,"libraries":0,"sourceHits":0,
  "pipelines":0,"archiveHits":0,"deferred":0,"failed":0,"seconds":12,"state":"paused","phase":1}}
"""))
check(legacy.startedByUser == nil, "A legacy journal has no starter")
check(legacy.progress.state == "paused", "A legacy interrupted run stays paused")
check(legacy.progress.completed == 3 && legacy.progress.total == 9, "A legacy run keeps its progress")
check(!WarmupResume.resumesByItself(legacy), "A run Prepare started must not resume by itself")
check(legacy.progress.stage == WarmupResume.interruptedStage, "A legacy run offers Resume in its words")

// The same legacy run saved while it was running (the app ended): paused, not resumed.
let legacyRunning = WarmupResume.restored(WarmupJournal(package: "Synthetic-1", title: "Synthetic game", progress: progress("running")))
check(legacyRunning.progress.state == "paused" && legacyRunning.interrupted == true, "A run saved as running reads as interrupted")
check(!WarmupResume.resumesByItself(legacyRunning), "A legacy run ended with the app must not resume by itself")

// A run the player started that iOS stopped resumes on activation, and keeps its words.
let started = WarmupResume.restored(WarmupJournal(package: "Synthetic-1", title: "Synthetic game",
    progress: progress("paused", stoppedStage), interrupted: true, startedByUser: true))
check(WarmupResume.resumesByItself(started), "A user-started run iOS stopped resumes")
check(started.progress.stage == stoppedStage, "A user-started run keeps its stop reason")

// A run the player started that ended with the app also resumes.
let startedRunning = WarmupResume.restored(WarmupJournal(package: "Synthetic-1", title: "Synthetic game",
    progress: progress("running"), startedByUser: true))
check(startedRunning.progress.state == "paused", "A user-started run saved as running reads as paused")
check(WarmupResume.resumesByItself(startedRunning), "A user-started run ended with the app resumes")

// Pause, finish and failure never resume by themselves.
for state in ["paused", "complete", "partial", "failed"] {
    let value = WarmupResume.restored(WarmupJournal(package: "Synthetic-1", title: "Synthetic game",
        progress: progress(state), startedByUser: true))
    check(!WarmupResume.resumesByItself(value), "A user-started \(state) run without an iOS stop must not resume")
}
for state in ["complete", "partial", "failed"] {
    let value = WarmupResume.restored(WarmupJournal(package: "Synthetic-1", title: "Synthetic game",
        progress: progress(state), interrupted: true, startedByUser: true))
    check(!WarmupResume.resumesByItself(value), "A \(state) run must not resume")
    check(value.progress.state == state, "Restoring keeps a \(state) run as it was")
}

// The flag survives the save and read the job does, and an absent flag stays absent.
let saved = try JSONEncoder().encode(started)
check(String(decoding: saved, as: UTF8.self).contains("\"startedByUser\":true"), "The starter is saved with the job")
let reread = try JSONDecoder().decode(WarmupJournal.self, from: saved)
check(WarmupResume.resumesByItself(reread), "A saved user-started run still resumes")
let legacySaved = try JSONEncoder().encode(legacy)
check(!String(decoding: legacySaved, as: UTF8.self).contains("startedByUser"), "A legacy journal gains no starter when saved again")

print("Warm-up resume checks passed: \(checks)")
