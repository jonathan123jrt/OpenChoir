#pragma once

#include <juce_core/juce_core.h>

namespace openchoir::paths
{

/** Where the AI engine lives. Override with OPENCHOIR_AI_DIR. */
inline juce::File aiDir()
{
    auto env = juce::SystemStats::getEnvironmentVariable ("OPENCHOIR_AI_DIR", {});
    if (env.isNotEmpty())
        return juce::File (env);
   #if JUCE_MAC
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("Application Support/OpenChoir/ai");
   #elif JUCE_WINDOWS
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("OpenChoir/ai");
   #else
    return juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile (".local/share/OpenChoir/ai");
   #endif
}

/** The engine's private Python. Override with OPENCHOIR_PYTHON. */
inline juce::File python()
{
    auto env = juce::SystemStats::getEnvironmentVariable ("OPENCHOIR_PYTHON", {});
    if (env.isNotEmpty())
        return juce::File (env);
   #if JUCE_WINDOWS
    return aiDir().getChildFile (".venv/Scripts/python.exe");
   #else
    return aiDir().getChildFile (".venv/bin/python");
   #endif
}

inline juce::File jobScript()  { return aiDir().getChildFile ("openchoir_job.py"); }
inline juce::File setupScript() { return aiDir().getChildFile ("setup.command"); }
inline juce::File voiceBank()  { return aiDir().getChildFile ("voices/vocalset/voices.json"); }

enum class EngineState { ready, notInstalled, voicesMissing };

inline EngineState engineState()
{
    if (! jobScript().existsAsFile() || ! python().existsAsFile())
        return EngineState::notInstalled;
    if (! voiceBank().existsAsFile())
        return EngineState::voicesMissing;
    return EngineState::ready;
}

/** Renders and exports go here: ~/Music/OpenChoir/Renders/<date time> <vocal name>/ */
inline juce::File newSessionDir (const juce::String& vocalName)
{
    auto root = juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("OpenChoir/Renders");
    auto env = juce::SystemStats::getEnvironmentVariable ("OPENCHOIR_RENDERS_DIR", {});
    if (env.isNotEmpty()) root = juce::File (env);
    auto name = juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H%M%S ") + juce::File::createLegalFileName (vocalName);
    auto dir = root.getChildFile (name);
    dir.createDirectory();
    return dir;
}

} // namespace openchoir::paths
