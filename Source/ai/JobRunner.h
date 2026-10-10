#pragma once

#include <juce_core/juce_core.h>
#include "Paths.h"

namespace openchoir
{

/** Runs one render of the AI engine as a separate process and follows its
    progress lines (@@STATUS / @@PROGRESS / @@KEY / @@LAYER / @@DONE / @@ERROR).
    Everything the engine prints is also saved to job.log in the session folder. */
class JobRunner : private juce::Thread
{
public:
    enum class State { idle, running, done, failed, cancelled };

    JobRunner() : juce::Thread ("OpenChoir render") {}
    ~JobRunner() override { cancel(); stopThread (4000); }

    /** spec is written to <sessionDir>/job.json. */
    bool start (const juce::var& spec, const juce::File& sessionDir)
    {
        if (isThreadRunning()) return false;
        session = sessionDir;
        auto jobFile = session.getChildFile ("job.json");
        jobFile.replaceWithText (juce::JSON::toString (spec, false));
        {
            const juce::ScopedLock sl (lock);
            status = "Starting the AI engine";
            error.clear(); detectedKey.clear(); resultFile = juce::File();
        }
        progress = 0.0f; etaSeconds = -1; state = State::running; cancelRequested = false;
        startThread();
        return true;
    }

    void cancel()
    {
        cancelRequested = true;
        const juce::ScopedLock sl (procLock);
        if (process != nullptr) process->kill();
    }

    State  getState()    const noexcept { return state.load(); }
    float  getProgress() const noexcept { return progress.load(); }
    int    getEta()      const noexcept { return etaSeconds.load(); }
    juce::String getStatus() const      { const juce::ScopedLock sl (lock); return status; }
    juce::String getError()  const      { const juce::ScopedLock sl (lock); return error; }
    juce::String getDetectedKey() const { const juce::ScopedLock sl (lock); return detectedKey; }
    juce::File   getResultFile() const  { const juce::ScopedLock sl (lock); return resultFile; }
    juce::File   getLogFile() const     { return session.getChildFile ("job.log"); }

private:
    void run() override
    {
        juce::StringArray cmd { paths::python().getFullPathName(), "-u",
                                paths::jobScript().getFullPathName(),
                                session.getChildFile ("job.json").getFullPathName() };
        auto proc = std::make_unique<juce::ChildProcess>();
        juce::FileOutputStream log (getLogFile());
        log.setPosition (0); log.truncate();

        if (! proc->start (cmd, juce::ChildProcess::wantStdOut | juce::ChildProcess::wantStdErr))
        {
            fail ("Couldn't start the AI engine (" + paths::python().getFullPathName() + ").");
            return;
        }
        { const juce::ScopedLock sl (procLock); process = proc.get(); }

        // Read one byte at a time: ChildProcess reads with blocking fread(), so asking
        // for a large buffer would hold progress lines back until 4 KB had piled up.
        juce::MemoryBlock line;
        char c = 0;
        for (;;)
        {
            const int n = proc->readProcessOutput (&c, 1);
            if (n <= 0)
            {
                if (! proc->isRunning()) break;
                juce::Thread::sleep (20);
                continue;
            }
            log.write (&c, 1);
            if (c == '\n')
            {
                log.flush();
                handleLine (juce::String::fromUTF8 ((const char*) line.getData(), (int) line.getSize()).trimEnd());
                line.reset();
            }
            else
                line.append (&c, 1);
        }
        if (line.getSize() > 0)
            handleLine (juce::String::fromUTF8 ((const char*) line.getData(), (int) line.getSize()).trim());
        const auto exitCode = proc->getExitCode();
        { const juce::ScopedLock sl (procLock); process = nullptr; }

        if (cancelRequested)                { state = State::cancelled; setStatus ("Cancelled"); return; }
        if (state == State::failed)         return;
        if (getResultFile().existsAsFile()) { progress = 1.0f; state = State::done; setStatus ("Done"); return; }
        fail ("The AI engine stopped unexpectedly (exit code " + juce::String ((int) exitCode)
              + "). Details are in job.log.");
    }

    void handleLine (const juce::String& line)
    {
        if (! line.startsWith ("@@")) return;
        auto tag  = line.upToFirstOccurrenceOf (" ", false, false);
        auto rest = line.fromFirstOccurrenceOf (" ", false, false);
        if (tag == "@@STATUS")
            setStatus (rest);
        else if (tag == "@@PROGRESS")
        {
            auto parts = juce::StringArray::fromTokens (rest, " ", "");
            if (parts.size() >= 2)
            {
                progress = juce::jlimit (0.0f, 1.0f, parts[0].getFloatValue());
                etaSeconds = parts[1].getIntValue();
                parts.removeRange (0, 2);
                setStatus (parts.joinIntoString (" "));
            }
        }
        else if (tag == "@@KEY")
        {
            const juce::ScopedLock sl (lock);
            auto parts = juce::StringArray::fromTokens (rest, " ", "");
            detectedKey = parts.size() >= 2 ? parts[0] + " " + parts[1] : rest;
        }
        else if (tag == "@@DONE")
        {
            const juce::ScopedLock sl (lock);
            resultFile = juce::File (rest.trim());
        }
        else if (tag == "@@ERROR")
            fail (rest);
    }

    void setStatus (const juce::String& s) { const juce::ScopedLock sl (lock); status = s; }
    void fail (const juce::String& msg)
    {
        { const juce::ScopedLock sl (lock); error = msg; status = "Failed"; }
        state = State::failed;
    }

    juce::File session;
    juce::CriticalSection lock, procLock;
    juce::ChildProcess* process = nullptr;
    juce::String status, error, detectedKey;
    juce::File resultFile;
    std::atomic<State> state { State::idle };
    std::atomic<float> progress { 0.0f };
    std::atomic<int> etaSeconds { -1 };
    std::atomic<bool> cancelRequested { false };
};

} // namespace openchoir
