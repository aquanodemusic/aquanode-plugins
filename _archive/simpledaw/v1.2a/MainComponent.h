#pragma once

#include <JuceHeader.h>
#include <set>
#include <vector>
#include <map>

//==============================================================================
// Automation point - simplified to per-step values
struct AutomationPoint
{
    int step;        // Which 32nd note step
    float value;     // Value (0.0 to 1.0)

    AutomationPoint() : step(0), value(0.5f) {}
    AutomationPoint(int s, float v) : step(s), value(v) {}

    bool operator<(const AutomationPoint& other) const
    {
        return step < other.step;
    }
};

//==============================================================================
// Automation lane - can be assigned to any VST parameter
class AutomationLane
{
public:
    AutomationLane() : parameterIndex(-1), pluginInstance(nullptr) {}

    void setStepValue(int step, float value)
    {
        if (step < 0) return;

        stepValues[step] = juce::jlimit(0.0f, 1.0f, value);
    }

    float getStepValue(int step) const
    {
        if (step < 0) return 0.5f;

        auto it = stepValues.find(step);
        if (it != stepValues.end())
            return it->second;

        // Linear interpolation between steps
        int prevStep = -1;
        int nextStep = -1;

        for (const auto& pair : stepValues)
        {
            if (pair.first < step)
                prevStep = pair.first;
            if (pair.first > step && nextStep == -1)
                nextStep = pair.first;
        }

        if (prevStep >= 0 && nextStep >= 0)
        {
            float prevValue = stepValues.at(prevStep);
            float nextValue = stepValues.at(nextStep);
            float t = static_cast<float>(step - prevStep) / static_cast<float>(nextStep - prevStep);
            return prevValue + t * (nextValue - prevValue);
        }
        else if (prevStep >= 0)
            return stepValues.at(prevStep);
        else if (nextStep >= 0)
            return stepValues.at(nextStep);

        return 0.5f;
    }

    void clearStep(int step)
    {
        stepValues.erase(step);
    }

    void clearAll()
    {
        stepValues.clear();
        parameterIndex = -1;
        pluginInstance = nullptr;
        assignmentName = "";
    }

    // Assignment to VST parameter
    void assignToParameter(juce::AudioPluginInstance* plugin, int paramIndex, const juce::String& name)
    {
        pluginInstance = plugin;
        parameterIndex = paramIndex;
        assignmentName = name;
    }

    bool isAssigned() const { return pluginInstance != nullptr && parameterIndex >= 0; }

    juce::AudioPluginInstance* getPlugin() const { return pluginInstance; }
    int getParameterIndex() const { return parameterIndex; }
    juce::String getAssignmentName() const { return assignmentName; }

    const std::map<int, float>& getAllStepValues() const { return stepValues; }

private:
    std::map<int, float> stepValues;  // Step -> Value mapping
    juce::AudioPluginInstance* pluginInstance;
    int parameterIndex;
    juce::String assignmentName;
};

//==============================================================================
// Structure to represent a note with position and length
struct Note
{
    int midiNote;      // MIDI note number (0-127)
    int startStep;     // Starting 32nd note position
    int length;        // Length in 32nd notes

    Note() : midiNote(0), startStep(0), length(1) {}
    Note(int note, int start, int len) : midiNote(note), startStep(start), length(len) {}

    bool operator<(const Note& other) const
    {
        if (midiNote != other.midiNote) return midiNote < other.midiNote;
        return startStep < other.startStep;
    }
};

//==============================================================================
// Scrollable Piano Roll Component with Note Labels and Track Colors
class PianoRollComponent : public juce::Component
{
public:
    PianoRollComponent()
    {
        // Initialize with 300 bars (10 minutes at 120 BPM), full MIDI range (128 notes, C-2 to G8)
        // Using 32nd note resolution (32 steps per bar)
        numBars = 300;
        stepsPerBar = 32; // 32nd notes
        numNotes = 128;
        cellWidth = 15; // Narrower to fit more steps
        cellHeight = 20;

        // Label width for note names
        labelWidth = 50;

        // Clear all notes for all tracks
        for (int track = 0; track < 3; ++track)
            trackNotes[track].clear();

        // Initialize 3 automation lanes (unassigned)
        automationLanes[0] = AutomationLane();
        automationLanes[1] = AutomationLane();
        automationLanes[2] = AutomationLane();

        // Setup viewport for scrolling
        viewport.setViewedComponent(&content, false);
        viewport.setScrollBarsShown(true, true);
        addAndMakeVisible(viewport);

        content.addMouseListener(this, false);
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colours::black);
    }

    void resized() override
    {
        viewport.setBounds(getLocalBounds());

        // Set content size based on piano roll dimensions + automation lanes
        int contentWidth = labelWidth + (numBars * stepsPerBar * cellWidth);
        int automationLaneHeight = 80;
        int automationSpacing = 10;
        int automationTotalHeight = (3 * automationLaneHeight) + (2 * automationSpacing) + 40; // 40 for top margin
        int contentHeight = numNotes * cellHeight + automationTotalHeight;
        content.setSize(contentWidth, contentHeight);
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        auto pos = e.getEventRelativeTo(&content).getPosition();

        // Check if we're clicking in the automation area
        int automationLaneHeight = 80;
        int automationStartY = numNotes * cellHeight + 20;
        int automationSpacing = 10;

        bool clickedAutomation = false;
        if (selectedAutomationLane >= 0 && selectedAutomationLane < 3)
        {
            int laneY = automationStartY + (selectedAutomationLane * (automationLaneHeight + automationSpacing));

            if (pos.y >= laneY && pos.y < laneY + automationLaneHeight)
            {
                clickedAutomation = true;

                // Handle automation editing
                int col = (pos.x - labelWidth) / cellWidth;
                if (col >= 0 && col < numBars * stepsPerBar)
                {
                    float normalizedY = static_cast<float>(pos.y - laneY) / automationLaneHeight;
                    float value = 1.0f - normalizedY; // Invert Y (top = 1.0, bottom = 0.0)
                    value = juce::jlimit(0.0f, 1.0f, value);

                    if (e.mods.isRightButtonDown())
                    {
                        // Right-click: delete automation point
                        automationLanes[selectedAutomationLane].clearStep(col);
                    }
                    else
                    {
                        // Left-click: set automation point
                        automationLanes[selectedAutomationLane].setStepValue(col, value);
                        isEditingAutomation = true;
                        lastAutomationCol = col;
                    }
                    content.repaint();
                }
            }
        }

        // Only handle note editing if we didn't click on automation
        if (!clickedAutomation)
        {
            if (e.mods.isRightButtonDown())
            {
                // Right-click to delete
                int col = (pos.x - labelWidth) / cellWidth;
                int row = pos.y / cellHeight;

                if (col >= 0 && col < numBars * stepsPerBar && row >= 0 && row < numNotes)
                {
                    int midiNote = numNotes - 1 - row;

                    // Delete note from all tracks at this position
                    for (int track = 0; track < 3; ++track)
                    {
                        deleteNoteAt(track, midiNote, col);
                    }
                    content.repaint();
                }
            }
            else
            {
                // Left-click to start drawing
                isDragging = true;
                dragStartCol = (pos.x - labelWidth) / cellWidth;
                dragStartRow = pos.y / cellHeight;
                dragEndCol = dragStartCol;

                if (dragStartCol >= 0 && dragStartCol < numBars * stepsPerBar &&
                    dragStartRow >= 0 && dragStartRow < numNotes)
                {
                    int midiNote = numNotes - 1 - dragStartRow;

                    // Delete any existing note at this position first
                    deleteNoteAt(currentTrack, midiNote, dragStartCol);

                    // Create new note with length 1 initially
                    Note newNote(midiNote, dragStartCol, 1);
                    trackNotes[currentTrack].push_back(newNote);
                    content.repaint();
                }
            }
        }
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        auto pos = e.getEventRelativeTo(&content).getPosition();

        // Handle automation dragging
        if (isEditingAutomation && selectedAutomationLane >= 0 && selectedAutomationLane < 3)
        {
            int automationLaneHeight = 80;
            int automationStartY = numNotes * cellHeight + 20;
            int automationSpacing = 10;
            int laneY = automationStartY + (selectedAutomationLane * (automationLaneHeight + automationSpacing));

            int col = (pos.x - labelWidth) / cellWidth;
            if (col >= 0 && col < numBars * stepsPerBar && col != lastAutomationCol)
            {
                float normalizedY = static_cast<float>(pos.y - laneY) / automationLaneHeight;
                float value = 1.0f - normalizedY;
                value = juce::jlimit(0.0f, 1.0f, value);

                automationLanes[selectedAutomationLane].setStepValue(col, value);
                lastAutomationCol = col;
                content.repaint();
            }
            return;
        }

        // Handle note dragging
        if (isDragging && !e.mods.isRightButtonDown())
        {
            int col = (pos.x - labelWidth) / cellWidth;
            int row = pos.y / cellHeight;

            if (col >= 0 && col < numBars * stepsPerBar && row >= 0 && row < numNotes)
            {
                // Only allow horizontal drag on the same row
                if (row == dragStartRow && col != dragEndCol)
                {
                    int midiNote = numNotes - 1 - dragStartRow;

                    // Find and update the note we're currently dragging
                    for (auto& note : trackNotes[currentTrack])
                    {
                        if (note.midiNote == midiNote && note.startStep == dragStartCol)
                        {
                            // Update length based on drag
                            int startCol = dragStartCol;
                            int endCol = col;

                            // Allow dragging both left and right
                            if (endCol >= startCol)
                            {
                                note.length = endCol - startCol + 1;
                            }
                            else
                            {
                                // Dragging backwards - adjust start position and length
                                note.startStep = endCol;
                                note.length = startCol - endCol + 1;
                                dragStartCol = endCol; // Update drag start for continued dragging
                            }

                            dragEndCol = col;
                            content.repaint();
                            break;
                        }
                    }
                }
            }
        }
    }

    void mouseUp(const juce::MouseEvent& e) override
    {
        isDragging = false;
        isEditingAutomation = false;
        lastAutomationCol = -1;
    }

    void setPlaying(bool playing)
    {
        isPlaying = playing;
        if (!playing)
            playheadPosition = 0.0;
        content.repaint();
    }

    void updatePlayhead(double position)
    {
        playheadPosition = position;
        content.repaint();
    }

    // Check if a note is active at a given step
    bool isNoteActiveAt(int track, int midiNote, int step) const
    {
        if (track < 0 || track >= 3 || midiNote < 0 || midiNote >= 128)
            return false;

        for (const auto& note : trackNotes[track])
        {
            if (note.midiNote == midiNote &&
                step >= note.startStep &&
                step < note.startStep + note.length)
            {
                return true;
            }
        }
        return false;
    }

    // Check if a note starts at a given step
    bool isNoteStartAt(int track, int midiNote, int step) const
    {
        if (track < 0 || track >= 3 || midiNote < 0 || midiNote >= 128)
            return false;

        for (const auto& note : trackNotes[track])
        {
            if (note.midiNote == midiNote && note.startStep == step)
            {
                return true;
            }
        }
        return false;
    }

    int getLastOccupiedStep() const
    {
        int lastStep = -1;
        for (int track = 0; track < 3; ++track)
        {
            for (const auto& note : trackNotes[track])
            {
                int noteEnd = note.startStep + note.length - 1;
                lastStep = juce::jmax(lastStep, noteEnd);
            }
        }
        // Return at least 15 (1 bar - 1) to ensure we have at least 16 steps
        return juce::jmax(15, lastStep);
    }

    void setCurrentTrack(int track)
    {
        if (track >= 0 && track < 3)
        {
            currentTrack = track;
            content.repaint();
        }
    }

    int getCurrentTrack() const { return currentTrack; }

    // Automation lane access (3 lanes: 0, 1, 2)
    AutomationLane& getAutomationLane(int laneIndex)
    {
        if (laneIndex >= 0 && laneIndex < 3)
            return automationLanes[laneIndex];
        return automationLanes[0]; // Fallback
    }

    const AutomationLane& getAutomationLane(int laneIndex) const
    {
        if (laneIndex >= 0 && laneIndex < 3)
            return automationLanes[laneIndex];
        return automationLanes[0]; // Fallback
    }

    void setSelectedAutomationLane(int laneIndex)
    {
        if (laneIndex >= -1 && laneIndex < 3)
        {
            selectedAutomationLane = laneIndex;
            content.repaint();
        }
    }

    int getSelectedAutomationLane() const
    {
        return selectedAutomationLane;
    }

    // Color customization
    void setBackgroundColor(const juce::Colour& color)
    {
        pianoRollBackgroundColor = color;
        content.repaint();
    }

    void setSidebarColor(const juce::Colour& color)
    {
        sidebarColor = color;
        content.repaint();
    }

    // Serialization methods
    juce::ValueTree getState() const
    {
        juce::ValueTree state("PianoRoll");

        // Save notes
        for (int track = 0; track < 3; ++track)
        {
            for (const auto& note : trackNotes[track])
            {
                juce::ValueTree noteNode("Note");
                noteNode.setProperty("track", track, nullptr);
                noteNode.setProperty("midiNote", note.midiNote, nullptr);
                noteNode.setProperty("startStep", note.startStep, nullptr);
                noteNode.setProperty("length", note.length, nullptr);
                state.appendChild(noteNode, nullptr);
            }
        }

        // Save automation lanes
        for (int laneIdx = 0; laneIdx < 3; ++laneIdx)
        {
            const auto& lane = automationLanes[laneIdx];
            juce::ValueTree laneNode("AutomationLane");
            laneNode.setProperty("index", laneIdx, nullptr);

            // Save assignment
            if (lane.isAssigned())
            {
                laneNode.setProperty("isAssigned", true, nullptr);
                laneNode.setProperty("assignmentName", lane.getAssignmentName(), nullptr);
                laneNode.setProperty("parameterIndex", lane.getParameterIndex(), nullptr);

                // Save plugin identifier (we'll match it during load)
                if (lane.getPlugin() != nullptr)
                {
                    laneNode.setProperty("pluginName", lane.getPlugin()->getName(), nullptr);
                }
            }
            else
            {
                laneNode.setProperty("isAssigned", false, nullptr);
            }

            // Save all automation step values
            const auto& stepValues = lane.getAllStepValues();
            for (const auto& pair : stepValues)
            {
                juce::ValueTree stepNode("AutomationPoint");
                stepNode.setProperty("step", pair.first, nullptr);
                stepNode.setProperty("value", pair.second, nullptr);
                laneNode.appendChild(stepNode, nullptr);
            }

            state.appendChild(laneNode, nullptr);
        }

        return state;
    }

    void setState(const juce::ValueTree& state)
    {
        // Clear all notes first
        for (int track = 0; track < 3; ++track)
            trackNotes[track].clear();

        // Clear all automation lanes
        for (int laneIdx = 0; laneIdx < 3; ++laneIdx)
            automationLanes[laneIdx].clearAll();

        // Load notes and automation lanes from state
        for (int i = 0; i < state.getNumChildren(); ++i)
        {
            auto childNode = state.getChild(i);

            if (childNode.hasType("Note"))
            {
                int track = childNode.getProperty("track");
                int midiNote = childNode.getProperty("midiNote");
                int startStep = childNode.getProperty("startStep");
                int length = childNode.getProperty("length");

                if (track >= 0 && track < 3)
                {
                    trackNotes[track].push_back(Note(midiNote, startStep, length));
                }
            }
            else if (childNode.hasType("AutomationLane"))
            {
                int laneIdx = childNode.getProperty("index");

                if (laneIdx >= 0 && laneIdx < 3)
                {
                    // Load automation points
                    for (int j = 0; j < childNode.getNumChildren(); ++j)
                    {
                        auto stepNode = childNode.getChild(j);
                        if (stepNode.hasType("AutomationPoint"))
                        {
                            int step = stepNode.getProperty("step");
                            float value = stepNode.getProperty("value");
                            automationLanes[laneIdx].setStepValue(step, value);
                        }
                    }

                    // Store assignment info for later restoration (after plugins are loaded)
                    if ((bool)childNode.getProperty("isAssigned"))
                    {
                        savedAutomationAssignments[laneIdx].isAssigned = true;
                        savedAutomationAssignments[laneIdx].assignmentName = childNode.getProperty("assignmentName");
                        savedAutomationAssignments[laneIdx].parameterIndex = childNode.getProperty("parameterIndex");
                        savedAutomationAssignments[laneIdx].pluginName = childNode.getProperty("pluginName");
                    }
                }
            }
        }

        content.repaint();
    }

    // Restore automation assignments after plugins are loaded
    void restoreAutomationAssignments(
        std::unique_ptr<juce::AudioPluginInstance>* plugins,
        std::unique_ptr<juce::AudioPluginInstance>(*effects)[3])
    {
        for (int laneIdx = 0; laneIdx < 3; ++laneIdx)
        {
            if (savedAutomationAssignments[laneIdx].isAssigned)
            {
                juce::String pluginName = savedAutomationAssignments[laneIdx].pluginName;
                int paramIdx = savedAutomationAssignments[laneIdx].parameterIndex;
                juce::String assignmentName = savedAutomationAssignments[laneIdx].assignmentName;

                // Try to find matching plugin
                juce::AudioPluginInstance* foundPlugin = nullptr;

                // Check instruments
                for (int track = 0; track < 3; ++track)
                {
                    if (plugins[track] != nullptr && plugins[track]->getName() == pluginName)
                    {
                        foundPlugin = plugins[track].get();
                        break;
                    }

                    // Check effects
                    for (int fx = 0; fx < 3; ++fx)
                    {
                        if (effects[track][fx] != nullptr && effects[track][fx]->getName() == pluginName)
                        {
                            foundPlugin = effects[track][fx].get();
                            break;
                        }
                    }
                    if (foundPlugin != nullptr) break;
                }

                // Restore assignment if plugin found
                if (foundPlugin != nullptr && paramIdx >= 0 && paramIdx < foundPlugin->getParameters().size())
                {
                    automationLanes[laneIdx].assignToParameter(foundPlugin, paramIdx, assignmentName);
                }

                // Clear saved assignment
                savedAutomationAssignments[laneIdx].isAssigned = false;
            }
        }

        content.repaint();
    }

    // MIDI import/export helper methods
    void addNoteToTrack(int track, const Note& note)
    {
        if (track >= 0 && track < 3)
        {
            trackNotes[track].push_back(note);
        }
    }

    void clearTrackNotes(int track)
    {
        if (track >= 0 && track < 3)
        {
            trackNotes[track].clear();
        }
    }

    const std::vector<Note>& getTrackNotes(int track) const
    {
        static std::vector<Note> emptyVector;
        if (track >= 0 && track < 3)
            return trackNotes[track];
        return emptyVector;
    }

private:
    void deleteNoteAt(int track, int midiNote, int step)
    {
        auto& notes = trackNotes[track];
        for (auto it = notes.begin(); it != notes.end(); )
        {
            if (it->midiNote == midiNote &&
                step >= it->startStep &&
                step < it->startStep + it->length)
            {
                it = notes.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    class PianoRollContent : public juce::Component
    {
    public:
        PianoRollContent(PianoRollComponent& owner) : pianoRoll(owner) {}

        void paint(juce::Graphics& g) override
        {
            g.fillAll(pianoRoll.pianoRollBackgroundColor);

            // Draw note labels sidebar with custom color
            g.setColour(pianoRoll.sidebarColor);
            g.fillRect(0, 0, pianoRoll.labelWidth, getHeight());

            g.setColour(juce::Colours::white);
            g.setFont(12.0f);

            const char* noteNames[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

            // Draw labels for visible notes (reversed so higher notes are at top)
            for (int i = 0; i < pianoRoll.numNotes; ++i)
            {
                int midiNote = pianoRoll.numNotes - 1 - i; // Reverse order
                int octave = (midiNote / 12) - 2; // MIDI note 0 = C-2
                int noteIndex = midiNote % 12;

                juce::String label = juce::String(noteNames[noteIndex]) + juce::String(octave);
                g.drawText(label, 5, i * pianoRoll.cellHeight, pianoRoll.labelWidth - 10,
                    pianoRoll.cellHeight, juce::Justification::centredLeft);
            }

            // Draw piano roll grid
            int gridStartX = pianoRoll.labelWidth;

            // Draw vertical lines (32nd notes)
            g.setColour(juce::Colours::darkgrey);
            for (int i = 0; i <= pianoRoll.numBars * pianoRoll.stepsPerBar; ++i)
            {
                int x = gridStartX + i * pianoRoll.cellWidth;

                // Emphasize bar lines (every stepsPerBar steps)
                if (i % pianoRoll.stepsPerBar == 0)
                {
                    g.setColour(juce::Colours::grey);
                    g.drawLine(x, 0, x, getHeight(), 2.0f);
                }
                else if (i % 4 == 0)
                {
                    g.setColour(juce::Colours::darkgrey.brighter(0.2f));
                    g.drawLine(x, 0, x, getHeight(), 1.0f);
                }
                else
                {
                    g.setColour(juce::Colours::darkgrey);
                    g.drawLine(x, 0, x, getHeight(), 0.5f);
                }
            }

            // Draw horizontal lines (notes)
            g.setColour(juce::Colours::darkgrey);
            for (int i = 0; i <= pianoRoll.numNotes; ++i)
            {
                int y = i * pianoRoll.cellHeight;
                g.drawLine(gridStartX, y, getWidth(), y, 0.5f);
            }

            // Draw notes for all tracks with track colors
            juce::Colour trackColors[3] = { juce::Colours::red, juce::Colours::green, juce::Colours::blue };

            for (int track = 0; track < 3; ++track)
            {
                juce::Colour noteColor = trackColors[track].withAlpha(track == pianoRoll.currentTrack ? 0.8f : 0.3f);
                g.setColour(noteColor);

                for (const auto& note : pianoRoll.trackNotes[track])
                {
                    int row = pianoRoll.numNotes - 1 - note.midiNote;
                    int startX = gridStartX + note.startStep * pianoRoll.cellWidth;
                    int width = note.length * pianoRoll.cellWidth;

                    g.fillRect(startX + 1, row * pianoRoll.cellHeight + 1,
                        width - 2, pianoRoll.cellHeight - 2);

                    // Draw a subtle border
                    g.setColour(trackColors[track].brighter());
                    g.drawRect((float)startX + 1.0f,
                        (float)(row * pianoRoll.cellHeight) + 1.0f,
                        (float)width - 2.0f,
                        (float)pianoRoll.cellHeight - 2.0f,
                        1.0f);

                    g.setColour(noteColor);
                }
            }

            // Draw playhead
            if (pianoRoll.isPlaying)
            {
                g.setColour(juce::Colours::yellow);
                int playheadX = gridStartX + static_cast<int>(pianoRoll.playheadPosition * pianoRoll.cellWidth);
                g.drawLine(playheadX, 0, playheadX, getHeight(), 2.0f);
            }

            // Draw automation lanes at the bottom
            int automationLaneHeight = 80;
            int automationStartY = pianoRoll.numNotes * pianoRoll.cellHeight + 20;
            juce::Colour automationColors[3] = { juce::Colours::orange, juce::Colours::cyan, juce::Colours::magenta };

            for (int laneIdx = 0; laneIdx < 3; ++laneIdx)
            {
                int laneY = automationStartY + (laneIdx * (automationLaneHeight + 10));

                // Draw sidebar background for automation lane label
                g.setColour(pianoRoll.sidebarColor);
                g.fillRect(0, laneY, pianoRoll.labelWidth, automationLaneHeight);

                // Draw lane background
                g.setColour(juce::Colours::darkgrey.darker());
                g.fillRect(gridStartX, laneY, pianoRoll.numBars * pianoRoll.stepsPerBar * pianoRoll.cellWidth, automationLaneHeight);

                // Draw lane border
                g.setColour(juce::Colours::grey);
                g.drawRect((float)gridStartX, (float)laneY, (float)pianoRoll.numBars * pianoRoll.stepsPerBar * pianoRoll.cellWidth, (float)automationLaneHeight, 1.0f);

                // Draw lane label
                g.setColour(juce::Colours::white);
                g.setFont(14.0f);
                juce::String laneName = "Aut" + juce::String(laneIdx + 1);
                if (pianoRoll.automationLanes[laneIdx].isAssigned())
                {
                    //laneName += ": " + pianoRoll.automationLanes[laneIdx].getAssignmentName();
                }
                else
                {
                    //laneName += " (Not Assigned)";
                }
                g.drawText(laneName, 5, laneY, pianoRoll.labelWidth - 10, 20, juce::Justification::centredLeft);

                // Highlight selected lane
                if (pianoRoll.getSelectedAutomationLane() == laneIdx)
                {
                    g.setColour(juce::Colours::yellow.withAlpha(0.3f));
                    g.fillRect(gridStartX, laneY, pianoRoll.numBars * pianoRoll.stepsPerBar * pianoRoll.cellWidth, automationLaneHeight);
                }

                // Draw automation curve
                const auto& lane = pianoRoll.automationLanes[laneIdx];
                g.setColour(automationColors[laneIdx]);

                // Draw vertical grid lines for steps
                g.setColour(juce::Colours::darkgrey.brighter(0.1f));
                for (int step = 0; step <= pianoRoll.numBars * pianoRoll.stepsPerBar; ++step)
                {
                    int x = gridStartX + step * pianoRoll.cellWidth;
                    if (step % 4 == 0)
                    {
                        g.drawLine(x, laneY, x, laneY + automationLaneHeight, 0.5f);
                    }
                }

                // Draw center line (0.5 value)
                g.setColour(juce::Colours::grey.darker());
                g.drawLine(gridStartX, laneY + automationLaneHeight / 2,
                    gridStartX + pianoRoll.numBars * pianoRoll.stepsPerBar * pianoRoll.cellWidth,
                    laneY + automationLaneHeight / 2, 1.0f);

                // Draw automation points and curve
                juce::Path automationPath;
                bool pathStarted = false;

                for (int step = 0; step < pianoRoll.numBars * pianoRoll.stepsPerBar; ++step)
                {
                    float value = lane.getStepValue(step);
                    int x = gridStartX + step * pianoRoll.cellWidth + pianoRoll.cellWidth / 2;
                    int y = laneY + automationLaneHeight - static_cast<int>(value * automationLaneHeight);

                    if (!pathStarted)
                    {
                        automationPath.startNewSubPath(x, y);
                        pathStarted = true;
                    }
                    else
                    {
                        automationPath.lineTo(x, y);
                    }
                }

                // Draw the curve
                g.setColour(automationColors[laneIdx].withAlpha(0.8f));
                g.strokePath(automationPath, juce::PathStrokeType(2.0f));

                // Draw explicit automation points (where user set values)
                const auto& stepValues = lane.getAllStepValues();
                for (const auto& pair : stepValues)
                {
                    int step = pair.first;
                    float value = pair.second;

                    if (step >= 0 && step < pianoRoll.numBars * pianoRoll.stepsPerBar)
                    {
                        int x = gridStartX + step * pianoRoll.cellWidth + pianoRoll.cellWidth / 2;
                        int y = laneY + automationLaneHeight - static_cast<int>(value * automationLaneHeight);

                        // Draw point
                        g.setColour(automationColors[laneIdx]);
                        g.fillEllipse(x - 4, y - 4, 8, 8);

                        // Draw white border around point
                        g.setColour(juce::Colours::white);
                        g.drawEllipse(x - 4, y - 4, 8, 8, 1.5f);
                    }
                }
            }
        }

    private:
        PianoRollComponent& pianoRoll;
    };

    PianoRollContent content{ *this };
    juce::Viewport viewport;

    int numBars;
    int stepsPerBar; // Number of steps per bar (32 for 32nd notes)
    int numNotes;
    int cellWidth;
    int cellHeight;
    int labelWidth;

    std::vector<Note> trackNotes[3];  // Notes for each track
    AutomationLane automationLanes[3];  // 3 assignable automation lanes
    int currentTrack = 0;
    int selectedAutomationLane = -1;  // -1 = none, 0-2 = lane index

    // Saved automation assignments (for loading projects)
    struct SavedAutomationAssignment
    {
        bool isAssigned = false;
        juce::String assignmentName;
        juce::String pluginName;
        int parameterIndex = -1;
        bool isInstrument = true;  // true = instrument, false = effect
        int track = -1;           // Which track (0-2)
        int effectSlot = -1;      // Which effect slot (0-2), only used if !isInstrument
    };
    SavedAutomationAssignment savedAutomationAssignments[3];

    bool isDragging = false;
    bool isEditingAutomation = false;
    int lastAutomationCol = -1;
    int dragStartCol = 0;
    int dragStartRow = 0;
    int dragEndCol = 0;

    bool isPlaying = false;
    double playheadPosition = 0.0;

    juce::Colour pianoRollBackgroundColor = juce::Colours::black;
    juce::Colour sidebarColor = juce::Colours::darkgrey;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PianoRollComponent)
};

//==============================================================================
// Per-track audio processor with VST support
class TrackAudioProcessor
{
public:
    TrackAudioProcessor()
    {
        vstInstrument = nullptr;
        for (int i = 0; i < 3; ++i)
            vstEffects[i] = nullptr;
    }

    void prepareToPlay(double sampleRate, int samplesPerBlock)
    {
        if (vstInstrument != nullptr)
            vstInstrument->prepareToPlay(sampleRate, samplesPerBlock);

        for (int i = 0; i < 3; ++i)
        {
            if (vstEffects[i] != nullptr)
                vstEffects[i]->prepareToPlay(sampleRate, samplesPerBlock);
        }
    }

    void releaseResources()
    {
        if (vstInstrument != nullptr)
            vstInstrument->releaseResources();

        for (int i = 0; i < 3; ++i)
        {
            if (vstEffects[i] != nullptr)
                vstEffects[i]->releaseResources();
        }
    }

    void processBlock(float* const* outputChannelData, int numOutputChannels, int numSamples,
        juce::MidiBuffer& midiMessages, juce::AudioBuffer<float>* sidechainInput = nullptr)
    {
        // Create audio buffer
        juce::AudioBuffer<float> buffer(numOutputChannels, numSamples);
        buffer.clear();

        // Process through instrument if loaded
        if (vstInstrument != nullptr)
        {
            vstInstrument->processBlock(buffer, midiMessages);
        }

        // Process through effect chain
        for (int i = 0; i < 3; ++i)
        {
            if (vstEffects[i] != nullptr)
            {
                juce::MidiBuffer emptyMidi;

                // Check if plugin supports sidechain (has more than 2 input channels)
                int pluginInputChannels = vstEffects[i]->getTotalNumInputChannels();

                if (sidechainInput != nullptr && pluginInputChannels > numOutputChannels)
                {
                    // Plugin supports sidechain - create buffer with sidechain channels
                    juce::AudioBuffer<float> bufferWithSidechain(pluginInputChannels, numSamples);

                    // Copy main signal to first channels
                    for (int ch = 0; ch < numOutputChannels && ch < pluginInputChannels; ++ch)
                    {
                        bufferWithSidechain.copyFrom(ch, 0, buffer, ch, 0, numSamples);
                    }

                    // Copy sidechain signal to additional channels (typically channels 2-3 for stereo sidechain)
                    int sidechainStartChannel = numOutputChannels;
                    for (int ch = 0; ch < sidechainInput->getNumChannels() && (sidechainStartChannel + ch) < pluginInputChannels; ++ch)
                    {
                        bufferWithSidechain.copyFrom(sidechainStartChannel + ch, 0, *sidechainInput, ch, 0, numSamples);
                    }

                    // Process with sidechain
                    vstEffects[i]->processBlock(bufferWithSidechain, emptyMidi);

                    // Copy back to main buffer (only output channels)
                    for (int ch = 0; ch < numOutputChannels; ++ch)
                    {
                        buffer.copyFrom(ch, 0, bufferWithSidechain, ch, 0, numSamples);
                    }
                }
                else
                {
                    // No sidechain support or no sidechain input - process normally
                    vstEffects[i]->processBlock(buffer, emptyMidi);
                }
            }
        }

        // Copy to output
        for (int ch = 0; ch < numOutputChannels; ++ch)
        {
            juce::FloatVectorOperations::copy(outputChannelData[ch], buffer.getReadPointer(ch), numSamples);
        }
    }

    void setVSTInstrument(juce::AudioPluginInstance* instance)
    {
        vstInstrument = instance;
    }

    void setVSTEffect(int slot, juce::AudioPluginInstance* instance)
    {
        if (slot >= 0 && slot < 3)
            vstEffects[slot] = instance;
    }

    juce::AudioPluginInstance* getVSTInstrument() const
    {
        return vstInstrument;
    }

    juce::AudioPluginInstance* getVSTEffect(int slot) const
    {
        if (slot >= 0 && slot < 3)
            return vstEffects[slot];
        return nullptr;
    }

private:
    juce::AudioPluginInstance* vstInstrument;
    juce::AudioPluginInstance* vstEffects[3];
};

//==============================================================================
// Main audio processor combining all tracks
class AudioProcessor : public juce::AudioIODeviceCallback
{
public:
    AudioProcessor()
    {
        pianoRoll = nullptr;
    }

    // Add keyboard MIDI note from UI thread (thread-safe)
    void addKeyboardMidiNote(int track, int midiNote, bool isNoteOn, int velocity = 100)
    {
        if (track < 0 || track >= 3)
            return;

        juce::MidiMessage message = isNoteOn ? 
            juce::MidiMessage::noteOn(1, midiNote, (juce::uint8)velocity) :
            juce::MidiMessage::noteOff(1, midiNote);

        const juce::ScopedLock sl(keyboardMidiLock);
        keyboardMidiQueue[track].addEvent(message, 0);
    }

    void audioDeviceIOCallbackWithContext(const float* const* inputChannelData,
        int numInputChannels,
        float* const* outputChannelData,
        int numOutputChannels,
        int numSamples,
        const juce::AudioIODeviceCallbackContext& context) override
    {
        // Clear output
        for (int ch = 0; ch < numOutputChannels; ++ch)
        {
            juce::FloatVectorOperations::clear(outputChannelData[ch], numSamples);
        }

        if (!isPlaying || pianoRoll == nullptr)
            return;

        // Calculate step timing (32nd notes = 8 per beat)
        double beatsPerSecond = bpm.load() / 60.0;
        double thirtySecondNotesPerSecond = beatsPerSecond * 8.0;
        int samplesPerStep = static_cast<int>(sampleRate / thirtySecondNotesPerSecond);

        // Get loop length
        int loopLength = pianoRoll->getLastOccupiedStep() + 1;

        // Prepare MIDI buffers for each track
        juce::MidiBuffer trackMidiBuffers[3];

        // Add queued keyboard MIDI messages to track buffers (thread-safe)
        {
            const juce::ScopedLock sl(keyboardMidiLock);
            for (int track = 0; track < 3; ++track)
            {
                if (!keyboardMidiQueue[track].isEmpty())
                {
                    trackMidiBuffers[track].addEvents(keyboardMidiQueue[track], 0, numSamples, 0);
                    keyboardMidiQueue[track].clear();
                }
            }
        }

        // Process each sample to handle note timing accurately
        for (int sample = 0; sample < numSamples; ++sample)
        {
            juce::int64 globalSample = currentSample + sample;
            int currentStep = static_cast<int>((globalSample / samplesPerStep) % loopLength);
            int sampleInStep = static_cast<int>(globalSample % samplesPerStep);

            // Apply automation at step boundaries
            if (sampleInStep == 0 && pianoRoll != nullptr)
            {
                // Apply all 3 automation lanes to their assigned parameters
                for (int lane = 0; lane < 3; ++lane)
                {
                    const AutomationLane& autoLane = pianoRoll->getAutomationLane(lane);
                    if (autoLane.isAssigned())
                    {
                        float value = autoLane.getStepValue(currentStep);
                        auto* plugin = autoLane.getPlugin();
                        int paramIndex = autoLane.getParameterIndex();

                        if (plugin != nullptr && paramIndex >= 0 && paramIndex < plugin->getParameters().size())
                        {
                            plugin->getParameters()[paramIndex]->setValue(value);
                        }
                    }
                }
            }

            // Trigger notes at the start of their step
            if (sampleInStep == 0)
            {
                for (int track = 0; track < 3; ++track)
                {
                    for (int midiNote = 0; midiNote < 128; ++midiNote)
                    {
                        bool wasActive = activeNotes[track][midiNote];
                        bool isActive = pianoRoll->isNoteActiveAt(track, midiNote, currentStep);
                        bool isStart = pianoRoll->isNoteStartAt(track, midiNote, currentStep);

                        if (isActive && isStart && !wasActive)
                        {
                            // Note starts - send note on
                            trackMidiBuffers[track].addEvent(
                                juce::MidiMessage::noteOn(1, midiNote, (juce::uint8)100), sample);
                            activeNotes[track][midiNote] = true;
                        }
                        else if (!isActive && wasActive)
                        {
                            // Note ends - send note off
                            trackMidiBuffers[track].addEvent(
                                juce::MidiMessage::noteOff(1, midiNote), sample);
                            activeNotes[track][midiNote] = false;
                        }
                    }
                }
            }
        }

        // Mix all tracks with sidechain routing
        juce::AudioBuffer<float> trackBuffer(numOutputChannels, numSamples);
        juce::AudioBuffer<float> track3Buffer(numOutputChannels, numSamples); // Capture Track 3 for sidechain
        juce::AudioBuffer<float> mixBuffer(numOutputChannels, numSamples);
        mixBuffer.clear();

        // Process Track 3 first (to use as sidechain for Track 2)
        trackBuffer.clear();
        tracks[2].processBlock(trackBuffer.getArrayOfWritePointers(),
            numOutputChannels, numSamples, trackMidiBuffers[2], nullptr);

        // Copy Track 3 output for sidechain use (BEFORE volume is applied)
        for (int ch = 0; ch < numOutputChannels; ++ch)
        {
            track3Buffer.copyFrom(ch, 0, trackBuffer, ch, 0, numSamples);
        }

        // Apply Track 3 volume control to its OUTPUT (not the sidechain signal)
        float t3Vol = track3Volume.load();
        for (int ch = 0; ch < numOutputChannels; ++ch)
        {
            trackBuffer.applyGain(ch, 0, numSamples, t3Vol);
        }

        // Mix Track 3 into output (with volume applied)
        for (int ch = 0; ch < numOutputChannels; ++ch)
        {
            mixBuffer.addFrom(ch, 0, trackBuffer, ch, 0, numSamples);
        }

        // Process Track 2 WITH sidechain from Track 3 (unaffected by Track 3 volume)
        trackBuffer.clear();

        // Apply sidechain amount
        juce::AudioBuffer<float> scaledSidechain(numOutputChannels, numSamples);
        for (int ch = 0; ch < numOutputChannels; ++ch)
        {
            scaledSidechain.copyFrom(ch, 0, track3Buffer, ch, 0, numSamples);
            scaledSidechain.applyGain(ch, 0, numSamples, sidechainAmount.load());
        }

        tracks[1].processBlock(trackBuffer.getArrayOfWritePointers(),
            numOutputChannels, numSamples, trackMidiBuffers[1], &scaledSidechain);

        // Mix Track 2 into output
        for (int ch = 0; ch < numOutputChannels; ++ch)
        {
            mixBuffer.addFrom(ch, 0, trackBuffer, ch, 0, numSamples);
        }

        // Process Track 1 (no sidechain)
        trackBuffer.clear();
        tracks[0].processBlock(trackBuffer.getArrayOfWritePointers(),
            numOutputChannels, numSamples, trackMidiBuffers[0], nullptr);

        // Mix Track 1 into output
        for (int ch = 0; ch < numOutputChannels; ++ch)
        {
            mixBuffer.addFrom(ch, 0, trackBuffer, ch, 0, numSamples);
        }

        // Copy to output
        for (int ch = 0; ch < numOutputChannels; ++ch)
        {
            juce::FloatVectorOperations::copy(outputChannelData[ch], mixBuffer.getReadPointer(ch), numSamples);
        }

        currentSample += numSamples;

        // Update playhead position
        if (pianoRoll != nullptr)
        {
            double beatsPerSecond = bpm.load() / 60.0;
            double thirtySecondNotesPerSecond = beatsPerSecond * 8.0;
            int samplesPerStep = static_cast<int>(sampleRate / thirtySecondNotesPerSecond);
            int loopLength = pianoRoll->getLastOccupiedStep() + 1;
            double position = static_cast<double>(currentSample % (loopLength * samplesPerStep)) / samplesPerStep;
            juce::MessageManager::callAsync([this, position]() {
                if (pianoRoll != nullptr)
                    pianoRoll->updatePlayhead(position);
                });
        }
    }

    void audioDeviceAboutToStart(juce::AudioIODevice* device) override
    {
        sampleRate = device->getCurrentSampleRate();

        for (int track = 0; track < 3; ++track)
        {
            tracks[track].prepareToPlay(sampleRate, device->getCurrentBufferSizeSamples());
        }
    }

    void audioDeviceStopped() override
    {
        for (int track = 0; track < 3; ++track)
        {
            tracks[track].releaseResources();
        }
    }

    void setPlaying(bool playing)
    {
        isPlaying = playing;
        if (!playing)
        {
            currentSample = 0;
            // Turn off all active notes on all tracks
            for (int track = 0; track < 3; ++track)
            {
                for (int i = 0; i < 128; ++i)
                    activeNotes[track][i] = false;
            }
        }
    }

    void setPianoRoll(PianoRollComponent* roll)
    {
        pianoRoll = roll;
    }

    void setBPM(double newBpm)
    {
        bpm.store(newBpm);
    }

    double getBPM() const
    {
        return bpm.load();
    }

    TrackAudioProcessor& getTrack(int trackIndex)
    {
        return tracks[trackIndex];
    }

    void setSidechainAmount(float amount)
    {
        sidechainAmount.store(juce::jlimit(0.0f, 1.0f, amount));
    }

    float getSidechainAmount() const
    {
        return sidechainAmount.load();
    }

    void setTrack3Volume(float volume)
    {
        track3Volume.store(juce::jlimit(0.0f, 1.0f, volume)); // 0 to 1x (100%)
    }

    float getTrack3Volume() const
    {
        return track3Volume.load();
    }

private:
    TrackAudioProcessor tracks[3];
    PianoRollComponent* pianoRoll = nullptr;
    std::atomic<bool> isPlaying{ false };
    std::atomic<double> bpm{ 120.0 };
    std::atomic<float> sidechainAmount{ 0.0f }; // 0.0 = no sidechain, 1.0 = full sidechain
    std::atomic<float> track3Volume{ 1.0f }; // 0.0 to 1.0 (0% to 100%)
    double sampleRate = 44100.0;
    juce::int64 currentSample = 0;
    bool activeNotes[3][128] = { {false}, {false}, {false} };
    
    // Keyboard MIDI input queue (thread-safe)
    juce::CriticalSection keyboardMidiLock;
    juce::MidiBuffer keyboardMidiQueue[3];
};

//==============================================================================
// Plugin GUI Window
class PluginWindow : public juce::DocumentWindow
{
public:
    PluginWindow(const juce::String& name, juce::AudioPluginInstance* pluginInstance)
        : DocumentWindow(name,
            juce::Desktop::getInstance().getDefaultLookAndFeel()
            .findColour(juce::ResizableWindow::backgroundColourId),
            DocumentWindow::allButtons),
        plugin(pluginInstance)
    {
        setUsingNativeTitleBar(true);

        if (plugin != nullptr && plugin->hasEditor())
        {
            // Check if editor already exists
            auto* editor = plugin->getActiveEditor();

            if (editor == nullptr)
            {
                // No editor exists, create one
                editor = plugin->createEditor();
            }

            if (editor != nullptr)
            {
                setContentOwned(editor, true);
                setResizable(editor->isResizable(), false);
            }
        }

        centreWithSize(getWidth(), getHeight());
        setVisible(true);
    }

    ~PluginWindow() override
    {
        // CRITICAL: Must clean up in the correct order
        // 1. Make invisible
        setVisible(false);

        // 2. Notify plugin if we have an editor
        if (plugin != nullptr)
        {
            auto* editor = plugin->getActiveEditor();
            if (editor != nullptr)
            {
                plugin->editorBeingDeleted(editor);
            }
        }

        // 3. Clear content BEFORE removing from desktop
        clearContentComponent();

        // 4. Remove from desktop
        removeFromDesktop();
    }

    void closeButtonPressed() override
    {
        // Just hide the window instead of deleting it
        setVisible(false);
    }

private:
    juce::AudioPluginInstance* plugin;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginWindow)
};

//==============================================================================
class MainComponent : public juce::Component,
    public juce::KeyListener
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    // KeyListener methods
    bool keyPressed(const juce::KeyPress& key, juce::Component* originatingComponent) override;
    bool keyStateChanged(bool isKeyDown, juce::Component* originatingComponent) override;

private:
    void loadVSTInstrument();
    void loadVSTEffect(int effectSlot);
    void startStop();
    void showLoadedPlugins();
    void saveProject();
    void loadProject();
    void exportToFLAC();
    void loadPluginFromDescription(const juce::PluginDescription& desc, bool isInstrument,
        int trackOrSlot, const juce::String& pluginStateBase64 = juce::String(), bool openGUI = true);
    void updateTrackButtons();
    void setCurrentTrack(int track);
    void openInstrumentGUI(int track);
    void openEffectGUI(int track, int effectSlot);

    // MIDI import/export functions
    void importMIDIToTrack(int track);
    void exportMIDIFromTrack(int track);

    // Track last touched VST parameter
    void setupParameterListeners(juce::AudioPluginInstance* plugin);

    // Individual listener for each plugin to correctly track which plugin was touched
// Individual listener for each plugin to correctly track which plugin was touched
    class PluginSpecificParameterListener : public juce::AudioProcessorParameter::Listener
    {
    public:
        // Structure to hold the value and name of a changed parameter
        struct ChangedParameterInfo
        {
            float value;
            juce::String name;
        };

        PluginSpecificParameterListener(MainComponent& owner, juce::AudioPluginInstance* pluginInstance)
            : mainComponent(owner), plugin(pluginInstance) {
        }

        void parameterValueChanged(int parameterIndex, float newValue) override
        {
            if (plugin != nullptr)
            {
                auto& params = plugin->getParameters();
                if (parameterIndex >= 0 && parameterIndex < params.size())
                {
                    // 1. Update global "last touched" for automation assignment
                    mainComponent.lastTouchedPlugin = plugin;
                    mainComponent.lastTouchedParameterIndex = parameterIndex;
                    mainComponent.lastTouchedParameterName = params[parameterIndex]->getName(32);

                    // 2. Record this change in our local history list
                    // We run this on the message thread to safely update the map
                    juce::String name = mainComponent.lastTouchedParameterName;

                    // Use a lock or MessageManager to ensure thread safety for the map
                    juce::MessageManager::callAsync([this, parameterIndex, newValue, name]()
                        {
                            ChangedParameterInfo info;
                            info.value = newValue;
                            info.name = name;
                            changedParams[parameterIndex] = info;
                        });
                }
            }
        }

        void parameterGestureChanged(int, bool) override {}

        juce::AudioPluginInstance* getPlugin() const { return plugin; }

        // Accessor to get the list of all parameters touched for this plugin
        const std::map<int, ChangedParameterInfo>& getChangedParams() const { return changedParams; }

        // Helper to manually add a param (used during loading)
        void forceAddChangedParam(int index, float val, const juce::String& name)
        {
            ChangedParameterInfo info;
            info.value = val;
            info.name = name;
            changedParams[index] = info;
        }

    private:
        MainComponent& mainComponent;
        juce::AudioPluginInstance* plugin;
        std::map<int, ChangedParameterInfo> changedParams; // Maps param index -> Info
    };

    // Manager for all parameter listeners
    class ParameterListenerManager
    {
    public:
        void addPluginListener(MainComponent& owner, juce::AudioPluginInstance* plugin)
        {
            if (plugin == nullptr)
                return;

            // Create a unique listener for this plugin
            auto listener = std::make_unique<PluginSpecificParameterListener>(owner, plugin);

            // Add this listener to all parameters of the plugin
            for (auto* param : plugin->getParameters())
            {
                param->addListener(listener.get());
            }

            // Store the listener
            listeners[plugin] = std::move(listener);
        }

        void removePluginListener(juce::AudioPluginInstance* plugin)
        {
            auto it = listeners.find(plugin);
            if (it != listeners.end())
            {
                // Remove listener from all parameters before destroying it
                if (plugin != nullptr)
                {
                    for (auto* param : plugin->getParameters())
                    {
                        param->removeListener(it->second.get());
                    }
                }
                listeners.erase(it);
            }
        }

        void clearAll()
        {
            // Clean up all listeners
            for (auto& pair : listeners)
            {
                if (pair.first != nullptr)
                {
                    for (auto* param : pair.first->getParameters())
                    {
                        param->removeListener(pair.second.get());
                    }
                }
            }
            listeners.clear();
        }

        // Add this method to ParameterListenerManager class
        std::map<int, PluginSpecificParameterListener::ChangedParameterInfo> getChangedParamsForPlugin(juce::AudioPluginInstance* plugin)
        {
            auto it = listeners.find(plugin);
            if (it != listeners.end())
            {
                return it->second->getChangedParams();
            }
            return {};
        }

        // Add this method to restore the list during project load
        void restoreChangedParam(juce::AudioPluginInstance* plugin, int index, float val, const juce::String& name)
        {
            auto it = listeners.find(plugin);
            if (it != listeners.end())
            {
                it->second->forceAddChangedParam(index, val, name);
            }
        }

    private:
        std::map<juce::AudioPluginInstance*, std::unique_ptr<PluginSpecificParameterListener>> listeners;
    };

    ParameterListenerManager parameterListenerManager;

    // Track selection buttons
    juce::TextButton trackButtons[3];

    // Per-track plugin load buttons
    juce::TextButton loadInstrumentButtons[3];
    juce::TextButton loadEffectButtons[3][3]; // [track][effect slot]

    // GUI open buttons
    juce::TextButton openInstrumentGUIButtons[3];
    juce::TextButton openEffectGUIButtons[3][3]; // [track][effect slot]

    // MIDI import/export buttons per track
    juce::TextButton importMIDIButtons[3];
    juce::TextButton exportMIDIButtons[3];

    juce::TextButton startStopButton;
    juce::TextButton showPluginsButton;
    juce::TextButton saveButton;
    juce::TextButton loadButton;
    juce::TextButton exportButton;

    juce::TextButton automationLaneButtons[3];  // Select which lane to edit
    juce::TextButton assignAutomationButtons[3];  // Assign lane to last touched parameter
    juce::Label automationLaneLabels[3];  // Show what's assigned
    juce::Label automationSectionLabel;  // "Automation Lanes" header

    juce::Label bpmLabel;
    juce::Slider bpmSlider;

    juce::Label sidechainLabel;
    juce::Slider sidechainSlider;

    juce::Label track3VolumeLabel;
    juce::Slider track3VolumeSlider;

    // Color customization controls
    juce::Label backgroundColorLabel;
    juce::TextEditor backgroundColorEditor;
    juce::Label pianoRollColorLabel;
    juce::TextEditor pianoRollColorEditor;

    PianoRollComponent pianoRoll;

    juce::AudioDeviceManager audioDeviceManager;
    AudioProcessor audioProcessor;

    std::unique_ptr<juce::AudioPluginInstance> vstInstruments[3];
    std::unique_ptr<juce::AudioPluginInstance> vstEffects[3][3]; // [track][effect slot]

    juce::AudioPluginFormatManager pluginFormatManager;
    juce::KnownPluginList knownPluginList;

    juce::PluginDescription savedInstrumentDesc[3];
    juce::PluginDescription savedEffectDesc[3][3];

    juce::String loadedInstrumentNames[3];
    juce::String loadedEffectNames[3][3];

    int currentTrack = 0;
    bool isPlaying = false;

    // Last touched VST parameter tracking
    juce::AudioPluginInstance* lastTouchedPlugin = nullptr;
    int lastTouchedParameterIndex = -1;
    juce::String lastTouchedParameterName = "";

    // PC Keyboard MIDI input
    int keyboardOctave = 4;  // Default octave (0-8)
    std::set<int> activeKeyboardNotes;  // Track which keys are currently pressed

    // Color customization
    juce::Colour backgroundColor = juce::Colours::darkgrey;
    juce::Colour pianoRollColor = juce::Colours::black;
    void updateBackgroundColor();
    void updatePianoRollColor();
    juce::Colour parseHexColor(const juce::String& hexString);

    // Plugin windows managed by JUCE's SafePointer
    juce::Component::SafePointer<PluginWindow> instrumentWindows[3];
    juce::Component::SafePointer<PluginWindow> effectWindows[3][3];

    // Track file choosers to ensure they're closed on shutdown
    juce::OwnedArray<juce::FileChooser> activeFileChoosers;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};