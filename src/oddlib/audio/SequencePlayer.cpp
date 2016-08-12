#include "oddlib/audio/SequencePlayer.h"

SequencePlayer::SequencePlayer(AliveAudio& aliveAudio)
    : mAliveAudio(aliveAudio)
{
    // Start the Sequencer thread.
    //m_SequenceThread = new std::thread(&SequencePlayer::m_PlayerThreadFunction, this);
}

SequencePlayer::~SequencePlayer()
{
    m_KillThread = true;
    //m_SequenceThread->join();
    //delete m_SequenceThread;
}

// Midi stuff
static void _SndMidiSkipLength(Oddlib::Stream& stream, int skip)
{
    stream.Seek(stream.Pos() + skip);
}

// Midi stuff
static u32 _MidiReadVarLen(Oddlib::Stream& stream)
{
    u32 ret = 0;
    u8 byte = 0;
    for (int i = 0; i < 4; ++i)
    {
        stream.ReadUInt8(byte);
        ret = (ret << 7) | (byte & 0x7f);
        if (!(byte & 0x80))
        {
            break;
        }
    }
    return ret;
}

double SequencePlayer::MidiTimeToSample(int time)
{
    // This may, or may not be correct. // TODO: Revise
    return ((60 * time) / m_SongTempo) * (AliveAudioSampleRate / 500.0);
}

// TODO: This thread spin locks
void SequencePlayer::m_PlayerThreadFunction()
{
    int channels[16] = {};

    //while (!m_KillThread)
    {
        m_PlayerStateMutex.lock();
        if (m_PlayerState == ALIVE_SEQUENCER_INIT_VOICES)
        {
            bool firstNote = true;
            std::lock_guard<std::recursive_mutex> notesLock(mAliveAudio.voiceListMutex);

            for (size_t i = 0; i < m_MessageList.size(); i++)
            {
                AliveAudioMidiMessage m = m_MessageList[i];
                switch (m.Type)
                {
                case ALIVE_MIDI_NOTE_ON:
                    mAliveAudio.NoteOn(channels[m.Channel], m.Note, m.Velocity, m_TrackID, MidiTimeToSample(m.TimeOffset));
                    if (firstNote)
                    {
                        m_SongBeginSample = static_cast<int>(mAliveAudio.currentSampleIndex + MidiTimeToSample(m.TimeOffset));
                        firstNote = false;
                    }
                    break;
                case ALIVE_MIDI_NOTE_OFF:
                    mAliveAudio.NoteOffDelay(channels[m.Channel], m.Note, m_TrackID, static_cast<float>(MidiTimeToSample(m.TimeOffset))); // Fix this. Make note off's have an offset in the voice timeline.
                    break;
                case ALIVE_MIDI_PROGRAM_CHANGE:
                    channels[m.Channel] = m.Special;
                    break;
                case ALIVE_MIDI_ENDTRACK:
                    m_PlayerState = ALIVE_SEQUENCER_PLAYING;
                    m_SongFinishSample = static_cast<Uint64>(mAliveAudio.currentSampleIndex + MidiTimeToSample(m.TimeOffset));
                    break;
                }

            }
        }
        m_PlayerStateMutex.unlock();

        if (m_PlayerState == ALIVE_SEQUENCER_PLAYING && mAliveAudio.currentSampleIndex > m_SongFinishSample)
        {
            m_PlayerState = ALIVE_SEQUENCER_FINISHED;

            // Give a quarter beat anyway
            DoQuaterCallback();
        }

        if (m_PlayerState == ALIVE_SEQUENCER_PLAYING)
        {
            const Uint64  quarterBeat = (m_SongFinishSample - m_SongBeginSample) / m_TimeSignatureBars;
            const int currentQuarterBeat = (int)(floor(GetPlaybackPositionSample() / quarterBeat));

            if (m_PrevBar != currentQuarterBeat)
            {
                m_PrevBar = currentQuarterBeat;
                DoQuaterCallback();
            }
        }
    }
}

Uint64 SequencePlayer::GetPlaybackPositionSample()
{
    return mAliveAudio.currentSampleIndex - m_SongBeginSample;
}

void SequencePlayer::StopSequence()
{
    mAliveAudio.ClearAllTrackVoices(m_TrackID);
    m_PlayerStateMutex.lock();
    m_PlayerState = ALIVE_SEQUENCER_STOPPED;
    m_PrevBar = 0;
    m_PlayerStateMutex.unlock();
}

void SequencePlayer::PlaySequence()
{
    m_PlayerStateMutex.lock();
    if (m_PlayerState == ALIVE_SEQUENCER_STOPPED || m_PlayerState == ALIVE_SEQUENCER_FINISHED)
    {
        m_PrevBar = 0;
        m_PlayerState = ALIVE_SEQUENCER_INIT_VOICES;
    }
    m_PlayerStateMutex.unlock();
}

int SequencePlayer::LoadSequenceData(std::vector<u8> seqData)
{
    Oddlib::Stream stream(std::move(seqData));

    return LoadSequenceStream(stream);
}

int SequencePlayer::LoadSequenceStream(Oddlib::Stream& stream)
{
    StopSequence();
    m_MessageList.clear();

    SeqHeader seqHeader;

    // Read the header

    stream.ReadUInt32(seqHeader.mMagic);
    stream.ReadUInt32(seqHeader.mVersion);
    stream.ReadUInt16(seqHeader.mResolutionOfQuaterNote);
    stream.ReadBytes(seqHeader.mTempo, sizeof(seqHeader.mTempo));
    stream.ReadUInt8(seqHeader.mTimeSignatureBars);
    stream.ReadUInt8(seqHeader.mTimeSignatureBeats);

    int tempoValue = 0;
    for (int i = 0; i < 3; i++)
    {
        tempoValue += seqHeader.mTempo[2 - i] << (8 * i);
    }

    m_TimeSignatureBars = seqHeader.mTimeSignatureBars;

    m_SongTempo = 60000000.0f / tempoValue;

    //int channels[16];

    unsigned int deltaTime = 0;

    //const size_t midiDataStart = stream.Pos();

    // Context state
    SeqInfo gSeqInfo = {};

    for (;;)
    {
        // Read event delta time
        u32 delta = _MidiReadVarLen(stream);
        deltaTime += delta;
        //std::cout << "Delta: " << delta << " over all " << deltaTime << std::endl;

        // Obtain the event/status byte
        u8 eventByte = 0;
        stream.ReadUInt8(eventByte);
        if (eventByte < 0x80)
        {
            // Use running status
            if (!gSeqInfo.running_status) // v1
            {
                return 0; // error if no running status?
            }
            eventByte = gSeqInfo.running_status;

            // Go back one byte as the status byte isn't a status
            stream.Seek(stream.Pos() - 1);
        }
        else
        {
            // Update running status
            gSeqInfo.running_status = eventByte;
        }

        if (eventByte == 0xff)
        {
            // Meta event
            u8 metaCommand = 0;
            stream.ReadUInt8(metaCommand);

            u8 metaCommandLength = 0;
            stream.ReadUInt8(metaCommandLength);

            switch (metaCommand)
            {
            case 0x2f:
            {
                //std::cout << "end of track" << std::endl;
                m_MessageList.push_back(AliveAudioMidiMessage(ALIVE_MIDI_ENDTRACK, deltaTime, 0, 0, 0));
                return 0;
                /*
                int loopCount = gSeqInfo.iNumTimesToLoop;// v1 some hard coded data?? or just a local static?
                if (loopCount) // If zero then loop forever
                {
                --loopCount;

                //char buf[256];
                //sprintf(buf, "EOT: %d loops left\n", loopCount);
                // OutputDebugString(buf);

                gSeqInfo.iNumTimesToLoop = loopCount; //v1
                if (loopCount <= 0)
                {
                //getNext_q(aSeqIndex); // Done playing? Ptr not reset to start
                return 1;
                }
                }

                //OutputDebugString("EOT: Loop forever\n");
                // Must be a loop back to the start?
                stream.Seek(midiDataStart);
                */
            }

            case 0x51:    // Tempo in microseconds per quarter note (24-bit value)
            {
                //std::cout << "Temp change" << std::endl;
                // TODO: Not sure if this is correct
                u8 tempoByte = 0;
                //int t = 0;
                for (int i = 0; i < 3; i++)
                {
                    stream.ReadUInt8(tempoByte);
                    //t = tempoByte << 8 * i;
                }
            }
            break;

            default:
            {
                //std::cout << "Unknown meta event " << u32(metaCommand) << std::endl;
                // Skip unknown events
                // TODO Might be wrong
                _SndMidiSkipLength(stream, metaCommandLength);
            }
            }
        }
        else if (eventByte < 0x80)
        {
            // Error
            throw std::runtime_error("Unknown midi event");
        }
        else
        {
            const u8 channel = eventByte & 0xf;
            switch (eventByte >> 4)
            {
            case 0x9: // Note On
            {
                u8 note = 0;
                stream.ReadUInt8(note);

                u8 velocity = 0;
                stream.ReadUInt8(velocity);
                if (velocity == 0) // If velocity is 0, then the sequence means to do "Note Off"
                {
                    m_MessageList.push_back(AliveAudioMidiMessage(ALIVE_MIDI_NOTE_OFF, deltaTime, channel, note, velocity));
                }
                else
                {
                    m_MessageList.push_back(AliveAudioMidiMessage(ALIVE_MIDI_NOTE_ON, deltaTime, channel, note, velocity));
                }
            }
            break;
            case 0x8: // Note Off
            {
                u8 note = 0;
                stream.ReadUInt8(note);
                u8 velocity = 0;
                stream.ReadUInt8(velocity);

                m_MessageList.push_back(AliveAudioMidiMessage(ALIVE_MIDI_NOTE_OFF, deltaTime, channel, note, velocity));
            }
            break;
            case 0xc: // Program Change
            {
                u8 prog = 0;
                stream.ReadUInt8(prog);
                m_MessageList.push_back(AliveAudioMidiMessage(ALIVE_MIDI_PROGRAM_CHANGE, deltaTime, channel, 0, 0, prog));
            }
            break;
            case 0xa: // Polyphonic key pressure (after touch)
            {
                u8 note = 0;
                u8 pressure = 0;

                stream.ReadUInt8(note);
                stream.ReadUInt8(pressure);
            }
            break;
            case 0xb: // Controller Change
            {
                u8 controller = 0;
                u8 value = 0;
                stream.ReadUInt8(controller);
                stream.ReadUInt8(value);
            }
            break;
            case 0xd: // After touch
            {
                u8 value = 0;
                stream.ReadUInt8(value);
            }
            break;
            case 0xe: // Pitch Bend
            {
                u16 bend = 0;
                stream.ReadUInt16(bend);
            }
            break;
            case 0xf: // Sysex len
            {
                const u32 length = _MidiReadVarLen(stream);
                _SndMidiSkipLength(stream, length);
            }
            break;
            default:
                throw std::runtime_error("Unknown MIDI command");
            }

        }
    }
}
