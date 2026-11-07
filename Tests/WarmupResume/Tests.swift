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
// Earlier releases wrote this stop reason; a new run writes WarmupStopWords.stoppedByIOS.
let legacyStoppedStage = "Stopped by iOS while Satchel was in the background. It resumes when you return."
let stoppedStage = WarmupStopWords.stoppedByIOS

// A journal written before the field existed, by a warm-up Prepare started and iOS stopped:
// it stays paused, keeps its counts, and its words no longer promise an automatic resume.
let legacy = WarmupResume.restored(decode("""
{"package":"Synthetic-1","title":"Synthetic game","interrupted":true,
 "progress":{"stage":"\(legacyStoppedStage)","completed":3,"total":9,"shaders":2,"libraries":0,"sourceHits":0,
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

// A stop leaves one set of words: the paused and iOS stop reasons are distinct, and the iOS
// one promises the return the journal acts on.
check(WarmupStopWords.paused != WarmupStopWords.stoppedByIOS, "Pause and an iOS stop read differently")
check(WarmupStopWords.stoppedByIOS.contains("resumes when you return"), "An iOS stop says it resumes on return")
check(!WarmupStopWords.paused.contains("resumes when you"), "A pause does not promise to resume by itself")

// iOS's progress is whole units of finished work, never an empty total.
var units = WarmupProgress()
check(units.systemUnits.done == 0 && units.systemUnits.total == 1, "No units yet is nothing done of one")
units.unitsDone = 736; units.unitsTotal = 25_428 + 4 * 1_000
check(units.systemUnits.done == 736 && units.systemUnits.total == 29_428, "One unit per finished shader")
units.unitsDone = 12; units.unitsTotal = 10
check(units.systemUnits.done == 12 && units.systemUnits.total == 12, "Done never exceeds the total")
units.phase = 3; units.unitsDone = 0; units.unitsTotal = 0
check(units.systemUnits.done == 1 && units.systemUnits.total == 1, "A finished run with nothing to build is whole")
// Progress written by an earlier release has no units and still decodes.
let oldProgress = try JSONDecoder().decode(WarmupProgress.self, from: Data("""
{"stage":"paused","completed":2,"total":3,"shaders":2,"libraries":2,"sourceHits":0,"pipelines":0,
 "archiveHits":0,"deferred":0,"failed":0,"seconds":1,"state":"paused","phase":1}
""".utf8))
check(oldProgress.unitsDone == nil && oldProgress.unitsTotal == nil && oldProgress.systemUnits.total == 1, "Old progress decodes without units")
let savedUnits = try JSONDecoder().decode(WarmupProgress.self, from: JSONEncoder().encode(WarmupProgress(unitsDone: 5, unitsTotal: 9)))
check(savedUnits.unitsDone == 5 && savedUnits.unitsTotal == 9, "Units survive the save")

print("Warm-up resume checks passed: \(checks)")
