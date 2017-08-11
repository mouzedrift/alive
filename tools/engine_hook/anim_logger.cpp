#include "anim_logger.hpp"
#include "gamefilesystem.hpp"

// Hack to access private parts of ResourceMapper
#define private public

#include "resourcemapper.hpp"

#undef private

#include "window_hooks.hpp"
#include "debug_dialog.hpp"

Oddlib::AnimSerializer* AnimLogger::AddAnim(u32 id, u8* ptr, u32 size)
{
    auto it = mAnimCache.find(id);
    if (it == std::end(mAnimCache))
    {
        std::vector<u8> data(
            reinterpret_cast<u8*>(ptr),
            reinterpret_cast<u8*>(ptr) + size);

        Oddlib::MemoryStream ms(std::move(data));
        mAnimCache[id] = std::make_unique<Oddlib::AnimSerializer>(ms, false);
        return AddAnim(id, ptr, size);
    }
    else
    {
        return it->second.get();
    }
}

void AnimLogger::Add(u32 id, u32 idx, Oddlib::AnimSerializer* anim)
{
    auto key = std::make_pair(id, idx);
    auto it = mAnims.find(key);
    if (it == std::end(mAnims))
    {
        mAnims[key] = anim;
    }

    LogAnim(id, idx);
}

std::string AnimLogger::Find(u32 id, u32 idx)
{
    for (const auto& mapping : mResources->mAnimMaps)
    {
        for (const auto& location : mapping.second.mLocations)
        {
            if (location.mDataSetName == "AePc")
            {
                for (const auto& file : location.mFiles)
                {
                    if (file.mAnimationIndex == idx && file.mId == id)
                    {
                        return mapping.first;
                    }
                }
            }
        }
    }
    return "";
}

void AnimLogger::ResourcesInit()
{
    TRACE_ENTRYEXIT;
    mFileSystem = std::make_unique<GameFileSystem>();
    if (!mFileSystem->Init())
    {
        LOG_ERROR("File system init failure");
    }
    mResources = std::make_unique<ResourceMapper>(*mFileSystem,
        "../../data/dataset_contents.json",
        "../../data/animations.json",
        "../../data/sounds.json",
        "../../data/paths.json",
        "../../data/fmvs.json");
}

void AnimLogger::LogAnim(u32 id, u32 idx)
{
    if (GetAsyncKeyState(VK_F2))
    {
        std::cout << "RELOAD RESOURCES" << std::endl;
        mFileSystem = nullptr;
        mResources = nullptr;
    }

    if (!mFileSystem)
    {
        ResourcesInit();
    }

    std::string resName = Find(id, idx);
    if (resName != mLastResName)
    {
        std::string s = "id: " + std::to_string(id) + " idx: " + std::to_string(idx) + " resource name: " + resName;
        std::cout << "ANIM: " << s << std::endl; // use cout directly, don't want the function name etc here
        mLastResName = resName;

        gDebugUi->LogAnimation(resName);
    }

    //::Sleep(128);
}


AnimLogger& GetAnimLogger()
{
    static AnimLogger logger;
    return logger;
}
