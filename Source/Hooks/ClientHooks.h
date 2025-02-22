#pragma once

#include <Endpoints.h>
#include <Platform/Macros/CallingConventions.h>
#include <Platform/Macros/PlatformSpecific.h>
#include <RetSpoof/FunctionInvoker.h>

namespace csgo
{
    struct ClientPOD;
    enum class FrameStage;
    enum class UserMessageType;
}

void FASTCALL_CONV frameStageNotify(FASTCALL_THIS(csgo::ClientPOD* thisptr), csgo::FrameStage stage) noexcept;
bool FASTCALL_CONV dispatchUserMessage(FASTCALL_THIS(csgo::ClientPOD* thisptr), csgo::UserMessageType type, int passthroughFlags, int size, const void* data) noexcept;

template <typename HookImpl>
class ClientHooks {
public:
    void install(csgo::ClientPOD* client)
    {
        hookImpl.init(client);
        originalFrameStageNotify = reinterpret_cast<decltype(originalFrameStageNotify)>(hookImpl.hookAt(37, &frameStageNotify));
        originalDispatchUserMessage = reinterpret_cast<decltype(originalDispatchUserMessage)>(hookImpl.hookAt(38, &dispatchUserMessage));
    }

    void uninstall()
    {
        hookImpl.restore();
    }

    [[nodiscard]] auto getOriginalFrameStageNotify() const
    {
        return FunctionInvoker{ retSpoofGadgets->client, originalFrameStageNotify };
    }

    [[nodiscard]] auto getOriginalDispatchUserMessage() const
    {
        return FunctionInvoker{ retSpoofGadgets->client, originalDispatchUserMessage };
    }

private:
    HookImpl hookImpl;

    void (THISCALL_CONV* originalFrameStageNotify)(csgo::ClientPOD* thisptr, csgo::FrameStage stage);
    bool (THISCALL_CONV* originalDispatchUserMessage)(csgo::ClientPOD* thisptr, csgo::UserMessageType type, int passthroughFlags, int size, const void* data);
};
