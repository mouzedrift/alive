#include "oddlib/audio/AliveAudio.h"

AliveAudioSoundbank::~AliveAudioSoundbank()
{

}

AliveAudioSoundbank::AliveAudioSoundbank(std::string fileName, AliveAudio& aliveAudio)
{
    std::ifstream vbStream;
    std::ifstream vhStream;
    vbStream.open((fileName + ".VB").c_str(), std::ios::binary);
    vhStream.open((fileName + ".VH").c_str(), std::ios::binary);

    Vab mVab;
    mVab.ReadVh(vhStream);
    mVab.ReadVb(vbStream);

    InitFromVab(mVab, aliveAudio);

    vbStream.close();
    vhStream.close();
}

AliveAudioSoundbank::AliveAudioSoundbank(Oddlib::LvlArchive& archive, std::string vabID, AliveAudio& aliveAudio)
{
    Oddlib::LvlArchive::File * vhFile = archive.FileByName(vabID + ".VH");
    Oddlib::LvlArchive::File * vbFile = archive.FileByName(vabID + ".VB");

    std::vector<Uint8> vhData = vhFile->ChunkById(0)->ReadData();
    std::vector<Uint8> vbData = vbFile->ChunkById(0)->ReadData();

    std::stringstream vhStream;
    vhStream.write((const char *)vhData.data(), vhData.size());
    vhStream.seekg(0, std::ios_base::beg);

    std::stringstream vbStream;
    vbStream.write((const char *)vbData.data(), vbData.size());
    vbStream.seekg(0, std::ios_base::beg);

    Vab vab;
    vab.ReadVh(vhStream);
    vab.ReadVb(vbStream);

    InitFromVab(vab, aliveAudio);
}

AliveAudioSoundbank::AliveAudioSoundbank(std::string lvlPath, std::string vabID, AliveAudio& aliveAudio)
{
    Oddlib::LvlArchive archive = Oddlib::LvlArchive(lvlPath);
    Oddlib::LvlArchive::File * vhFile = archive.FileByName(vabID + ".VH");
    Oddlib::LvlArchive::File * vbFile = archive.FileByName(vabID + ".VB");

    std::vector<Uint8> vhData = vhFile->ChunkById(0)->ReadData();
    std::vector<Uint8> vbData = vbFile->ChunkById(0)->ReadData();

    std::stringstream vhStream;
    vhStream.write((const char *)vhData.data(), vhData.size());
    vhStream.seekg(0, std::ios_base::beg);

    std::stringstream vbStream;
    vbStream.write((const char *)vbData.data(), vbData.size());
    vbStream.seekg(0, std::ios_base::beg);

    Vab vab;
    vab.ReadVh(vhStream);
    vab.ReadVb(vbStream);

    InitFromVab(vab, aliveAudio);
}

void AliveAudioSoundbank::InitFromVab(Vab& mVab, AliveAudio& aliveAudio)
{
    for (size_t i = 0; i < mVab.iOffs.size(); i++)
    {
        auto sample = std::make_unique<AliveAudioSample>();
        if (mVab.iAoVags.size() > 0)
        {
            sample->mSampleSize = mVab.iAoVags[i]->iSize / sizeof(Uint16);
            sample->m_SampleBuffer.resize(mVab.iAoVags[i]->iSize);
            memcpy(sample->m_SampleBuffer.data(), mVab.iAoVags[i]->iSampleData.data(), sample->m_SampleBuffer.size() * sizeof(Uint16));
        }
        else
        {
            sample->mSampleSize = mVab.iOffs[i]->iLengthOrDuration / sizeof(Uint16);
            sample->m_SampleBuffer.resize(sample->mSampleSize);
            memcpy(sample->m_SampleBuffer.data(), aliveAudio.m_SoundsDat.data() + mVab.iOffs[i]->iFileOffset, sample->m_SampleBuffer.size() * sizeof(Uint16));

        }
        m_Samples.emplace_back(std::move(sample));
    }

    for (size_t i = 0; i < mVab.iAoVags.size(); i++)
    {
        auto sample = std::make_unique<AliveAudioSample>();
        sample->mSampleSize = mVab.iAoVags[i]->iSize / sizeof(Uint16);
        sample->m_SampleBuffer.resize(mVab.iAoVags[i]->iSize / sizeof(Uint16));
        memcpy(sample->m_SampleBuffer.data(), mVab.iAoVags[i]->iSampleData.data(), mVab.iAoVags[i]->iSize);
        m_Samples.emplace_back(std::move(sample));
    }

    for (int i = 0; i < 128; i++)
    {
        auto program = std::make_unique<AliveAudioProgram>();
        for (int t = 0; t < mVab.mProgs[i]->iNumTones; t++)
        {
            auto tone = std::make_unique<AliveAudioTone>();

            if (mVab.mProgs[i]->iTones[t]->iVag == 0) // Some Tones have vag 0? Essentially null?
            {
                continue;
            }

            tone->f_Volume = mVab.mProgs[i]->iTones[t]->iVol / 127.0f;
            tone->c_Center = mVab.mProgs[i]->iTones[t]->iCenter;
            tone->c_Shift = mVab.mProgs[i]->iTones[t]->iShift;
            tone->f_Pan = (mVab.mProgs[i]->iTones[t]->iPan / 64.0f) - 1.0f;
            tone->Min = mVab.mProgs[i]->iTones[t]->iMin;
            tone->Max = mVab.mProgs[i]->iTones[t]->iMax;
            tone->Pitch = mVab.mProgs[i]->iTones[t]->iShift / 100.0f;
            tone->m_Sample = m_Samples[mVab.mProgs[i]->iTones[t]->iVag - 1].get();
         
            unsigned short ADSR1 = mVab.mProgs[i]->iTones[t]->iAdsr1;
            unsigned short ADSR2 = mVab.mProgs[i]->iTones[t]->iAdsr2;

            REAL_ADSR realADSR = {};
            PSXConvADSR(&realADSR, ADSR1, ADSR2, false);

            tone->AttackTime = realADSR.attack_time;
            tone->DecayTime = realADSR.decay_time;
            tone->ReleaseTime = realADSR.release_time;
            tone->SustainTime = realADSR.sustain_time;

      
            if (realADSR.attack_time > 1) // This works until the loop database is added.
            {
                tone->Loop = true;
            }

            /*if (i == 27 || i == 81)
            tone->Loop = true;*/

            program->m_Tones.emplace_back(std::move(tone));
        }

        m_Programs.emplace_back(std::move(program));
    }
}
