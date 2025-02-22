#include "GlobalContext.h"

#if IS_WIN32()
#include <imgui/imgui_impl_dx9.h>
#include <imgui/imgui_impl_win32.h>

#include "Platform/Windows/DynamicLibrarySection.h"
#else
#include <imgui/imgui_impl_sdl.h>
#include <imgui/imgui_impl_opengl3.h>

#include "Platform/Linux/DynamicLibrarySection.h"
#endif

#include "EventListener.h"
#include "GameData.h"
#include "GUI.h"
#include "Hooks.h"
#include "InventoryChanger/InventoryChanger.h"
#include "Memory.h"
#include "Hacks/Aimbot.h"
#include "Hacks/Backtrack.h"
#include "Hacks/Chams.h"
#include "Hacks/EnginePrediction.h"
#include "Hacks/Glow.h"
#include "Hacks/Misc.h"
#include "Hacks/Sound.h"
#include "Hacks/StreamProofESP.h"
#include "Hacks/Triggerbot.h"
#include "Hacks/Visuals.h"
#include "CSGO/ClientClass.h"
#include "CSGO/Constants/ClassId.h"
#include "CSGO/Constants/FrameStage.h"
#include "CSGO/Constants/GameEventNames.h"
#include "CSGO/Constants/UserMessages.h"
#include "CSGO/Engine.h"
#include "CSGO/Entity.h"
#include "CSGO/EntityList.h"
#include "CSGO/GlobalVars.h"
#include "CSGO/InputSystem.h"
#include "CSGO/LocalPlayer.h"
#include "CSGO/ModelRender.h"
#include "CSGO/Recv.h"
#include "CSGO/SoundEmitter.h"
#include "CSGO/SoundInfo.h"
#include "CSGO/StudioRender.h"
#include "CSGO/Surface.h"
#include "CSGO/UserCmd.h"
#include "CSGO/ViewSetup.h"
#include "CSGO/PODs/RenderableInfo.h"

#include "Interfaces/ClientInterfaces.h"

GlobalContext::GlobalContext()
{
#if IS_WIN32()
    const windows_platform::DynamicLibrary clientDLL{ windows_platform::DynamicLibraryWrapper{}, csgo::CLIENT_DLL };
    const windows_platform::DynamicLibrary engineDLL{ windows_platform::DynamicLibraryWrapper{}, csgo::ENGINE_DLL };
#elif IS_LINUX()
    const linux_platform::SharedObject clientDLL{ linux_platform::DynamicLibraryWrapper{}, csgo::CLIENT_DLL };
    const linux_platform::SharedObject engineDLL{ linux_platform::DynamicLibraryWrapper{}, csgo::ENGINE_DLL };
#endif

    PatternNotFoundHandler patternNotFoundHandler;
    retSpoofGadgets.emplace(PatternFinder{ getCodeSection(clientDLL.getView()), patternNotFoundHandler }, PatternFinder{ getCodeSection(engineDLL.getView()), patternNotFoundHandler });
}

bool GlobalContext::createMoveHook(csgo::ClientMode* thisptr, float inputSampleTime, csgo::UserCmd* cmd)
{
    auto result = hooks->clientModeHooks.getOriginalCreateMove()(thisptr, inputSampleTime, cmd);

    if (!cmd->commandNumber)
        return result;

#if IS_WIN32()
    // bool& sendPacket = *reinterpret_cast<bool*>(*reinterpret_cast<std::uintptr_t*>(FRAME_ADDRESS()) - 0x1C);
    // since 19.02.2022 game update sendPacket is no longer on stack
    bool sendPacket = true;
#else
    bool dummy;
    bool& sendPacket = dummy;
#endif
    static auto previousViewAngles{ cmd->viewangles };
    const auto currentViewAngles{ cmd->viewangles };

    memory->globalVars->serverTime(cmd);
    features->misc.nadePredict();
    features->misc.antiAfkKick(cmd);
    features->misc.fastStop(cmd);
    features->misc.prepareRevolver(getEngineInterfaces().getEngine(), cmd);
    features->visuals.removeShadows();
    features->misc.runReportbot(getEngineInterfaces().getEngine());
    features->misc.bunnyHop(cmd);
    features->misc.autoStrafe(cmd);
    features->misc.removeCrouchCooldown(cmd);
    features->misc.autoPistol(cmd);
    features->misc.autoReload(cmd);
    features->misc.updateClanTag();
    features->misc.fakeBan(getEngineInterfaces().getEngine());
    features->misc.stealNames(getEngineInterfaces().getEngine());
    features->misc.revealRanks(cmd);
    features->misc.quickReload(cmd);
    features->misc.fixTabletSignal();
    features->misc.slowwalk(cmd);

    EnginePrediction::run(ClientInterfaces{ retSpoofGadgets->client, *clientInterfaces }, *memory, cmd);

    features->aimbot.run(features->misc, getEngineInterfaces(), ClientInterfaces{ retSpoofGadgets->client, *clientInterfaces }, getOtherInterfaces(), *config, *memory, cmd);
    Triggerbot::run(getEngineInterfaces().engineTrace(), getOtherInterfaces(), *memory, *config, cmd);
    features->backtrack.run(ClientInterfaces{ retSpoofGadgets->client, *clientInterfaces }, getEngineInterfaces(), getOtherInterfaces(), *memory, cmd);
    features->misc.edgejump(cmd);
    features->misc.moonwalk(cmd);
    features->misc.fastPlant(getEngineInterfaces().engineTrace(), cmd);

    if (!(cmd->buttons & (csgo::UserCmd::IN_ATTACK | csgo::UserCmd::IN_ATTACK2))) {
        features->misc.chokePackets(getEngineInterfaces().getEngine(), sendPacket);
    }

    auto viewAnglesDelta{ cmd->viewangles - previousViewAngles };
    viewAnglesDelta.normalize();
    viewAnglesDelta.x = std::clamp(viewAnglesDelta.x, -features->misc.maxAngleDelta(), features->misc.maxAngleDelta());
    viewAnglesDelta.y = std::clamp(viewAnglesDelta.y, -features->misc.maxAngleDelta(), features->misc.maxAngleDelta());

    cmd->viewangles = previousViewAngles + viewAnglesDelta;

    cmd->viewangles.normalize();
    features->misc.fixMovement(cmd, currentViewAngles.y);

    cmd->viewangles.x = std::clamp(cmd->viewangles.x, -89.0f, 89.0f);
    cmd->viewangles.y = std::clamp(cmd->viewangles.y, -180.0f, 180.0f);
    cmd->viewangles.z = 0.0f;
    cmd->forwardmove = std::clamp(cmd->forwardmove, -450.0f, 450.0f);
    cmd->sidemove = std::clamp(cmd->sidemove, -450.0f, 450.0f);

    previousViewAngles = cmd->viewangles;

    cmd->viewanglesBackup = cmd->viewangles;
    cmd->buttonsBackup = cmd->buttons;

    return false;
}

void GlobalContext::doPostScreenEffectsHook(csgo::ClientMode* thisptr, void* param)
{
    if (getEngineInterfaces().getEngine().isInGame()) {
        features->visuals.thirdperson();
        features->visuals.inverseRagdollGravity();
        features->visuals.reduceFlashEffect();
        features->visuals.updateBrightness();
        features->visuals.remove3dSky();
        features->glow.render(getEngineInterfaces(), ClientInterfaces{ retSpoofGadgets->client, *clientInterfaces }, *memory);
    }
    hooks->clientModeHooks.getOriginalDoPostScreenEffects()(thisptr, param);
}

float GlobalContext::getViewModelFovHook(csgo::ClientMode* thisptr)
{
    float additionalFov = features->visuals.viewModelFov();
    if (localPlayer) {
        if (const auto activeWeapon = csgo::Entity::from(retSpoofGadgets->client, localPlayer.get().getActiveWeapon()); activeWeapon.getPOD() != nullptr && activeWeapon.getNetworkable().getClientClass()->classId == ClassId::Tablet)
            additionalFov = 0.0f;
    }

    return hooks->clientModeHooks.getOriginalGetViewModelFov()(thisptr) + additionalFov;
}

void GlobalContext::drawModelExecuteHook(csgo::ModelRenderPOD* thisptr, void* ctx, void* state, const csgo::ModelRenderInfo& info, csgo::matrix3x4* customBoneToWorld)
{
    if (getOtherInterfaces().getStudioRender().isForcedMaterialOverride())
        return hooks->modelRenderHooks.getOriginalDrawModelExecute()(thisptr, ctx, state, &info, customBoneToWorld);

    if (features->visuals.removeHands(info.model->name) || features->visuals.removeSleeves(info.model->name) || features->visuals.removeWeapons(info.model->name))
        return;

    if (!features->chams.render(features->backtrack, *config, ctx, state, info, customBoneToWorld))
        hooks->modelRenderHooks.getOriginalDrawModelExecute()(thisptr, ctx, state, &info, customBoneToWorld);

    getOtherInterfaces().getStudioRender().forcedMaterialOverride(nullptr);
}

int GlobalContext::svCheatsGetIntHook(csgo::ConVarPOD* thisptr, ReturnAddress returnAddress)
{
    const auto original = hooks->svCheatsHooks.getOriginalSvCheatsGetInt()(thisptr);
    if (features->visuals.svCheatsGetBoolHook(returnAddress))
        return 1;
    return original;
}

void GlobalContext::frameStageNotifyHook(csgo::ClientPOD* thisptr, csgo::FrameStage stage)
{
    if (getEngineInterfaces().getEngine().isConnected() && !getEngineInterfaces().getEngine().isInGame())
        features->misc.changeName(getEngineInterfaces().getEngine(), true, nullptr, 0.0f);

    if (stage == csgo::FrameStage::START)
        GameData::update(ClientInterfaces{ retSpoofGadgets->client, *clientInterfaces }, getEngineInterfaces(), getOtherInterfaces(), *memory);

    if (stage == csgo::FrameStage::RENDER_START) {
        features->misc.preserveKillfeed();
        features->misc.disablePanoramablur();
        features->visuals.colorWorld();
        features->misc.updateEventListeners(getEngineInterfaces());
        features->visuals.updateEventListeners();
    }
    if (getEngineInterfaces().getEngine().isInGame()) {
        features->visuals.skybox(stage);
        features->visuals.removeBlur(stage);
        features->misc.oppositeHandKnife(stage);
        features->visuals.removeGrass(stage);
        features->visuals.modifySmoke(stage);
        features->visuals.disablePostProcessing(stage);
        features->visuals.removeVisualRecoil(stage);
        features->visuals.applyZoom(stage);
        features->misc.fixAnimationLOD(getEngineInterfaces().getEngine(), stage);
        features->backtrack.update(getEngineInterfaces(), ClientInterfaces{ retSpoofGadgets->client, *clientInterfaces }, getOtherInterfaces(), *memory, stage);
    }
    features->inventoryChanger.run(*memory, stage);

    hooks->clientHooks.getOriginalFrameStageNotify()(thisptr, stage);
}

int GlobalContext::emitSoundHook(csgo::EngineSoundPOD* thisptr, void* filter, int entityIndex, int channel, const char* soundEntry, unsigned int soundEntryHash, const char* sample, float volume, int seed, int soundLevel, int flags, int pitch, const csgo::Vector& origin, const csgo::Vector& direction, void* utlVecOrigins, bool updatePositions, float soundtime, int speakerentity, void* soundParams)
{
    Sound::modulateSound(ClientInterfaces{ retSpoofGadgets->client, *clientInterfaces }, *memory, soundEntry, entityIndex, volume);
    features->misc.autoAccept(soundEntry);

    volume = std::clamp(volume, 0.0f, 1.0f);
    return hooks->engineSoundHooks.getOriginalEmitSound()(thisptr, filter, entityIndex, channel, soundEntry, soundEntryHash, sample, volume, seed, soundLevel, flags, pitch, &origin, &direction, utlVecOrigins, updatePositions, soundtime, speakerentity, soundParams);
}

bool GlobalContext::shouldDrawFogHook(csgo::ClientMode* thisptr, ReturnAddress returnAddress)
{
#if IS_WIN32()
    if constexpr (std::is_same_v<HookType, MinHook>) {
        if (returnAddress != memory->shouldDrawFogReturnAddress)
            return hooks->clientModeHooks.getOriginalShouldDrawFog()(thisptr);
    }
#endif

    return !features->visuals.shouldRemoveFog();
}

bool GlobalContext::shouldDrawViewModelHook(csgo::ClientMode* thisptr)
{
    if (features->visuals.isZoomOn() && localPlayer && localPlayer.get().fov() < 45 && localPlayer.get().fovStart() < 45)
        return false;
    return hooks->clientModeHooks.getOriginalShouldDrawViewModel()(thisptr);
}

#if IS_WIN32()
void GlobalContext::lockCursorHook(csgo::SurfacePOD* thisptr)
{
    if (gui->isOpen())
        return getOtherInterfaces().getSurface().unlockCursor();
    return hooks->surfaceHooks.getOriginalLockCursor()(thisptr);
}
#endif

void GlobalContext::setDrawColorHook(csgo::SurfacePOD* thisptr, int r, int g, int b, int a, ReturnAddress returnAddress)
{
    features->visuals.setDrawColorHook(returnAddress, a);
    hooks->surfaceHooks.getOriginalSetDrawColor()(thisptr, r, g, b, a);
}

void GlobalContext::overrideViewHook(csgo::ClientMode* thisptr, csgo::ViewSetup* setup)
{
    if (localPlayer && !localPlayer.get().isScoped())
        setup->fov += features->visuals.fov();
    setup->farZ += features->visuals.farZ() * 10;
    hooks->clientModeHooks.getOriginalOverrideView()(thisptr, setup);
}

int GlobalContext::dispatchSoundHook(csgo::SoundInfo& soundInfo)
{
    if (const char* soundName = getOtherInterfaces().getSoundEmitter().getSoundName(soundInfo.soundIndex)) {
        Sound::modulateSound(ClientInterfaces{ retSpoofGadgets->client, *clientInterfaces }, *memory, soundName, soundInfo.entityIndex, soundInfo.volume);
        soundInfo.volume = std::clamp(soundInfo.volume, 0.0f, 1.0f);
    }
    return hooks->originalDispatchSound(soundInfo);
}

void GlobalContext::render2dEffectsPreHudHook(csgo::ViewRender* thisptr, void* viewSetup)
{
    features->visuals.applyScreenEffects();
    features->visuals.hitEffect();
    hooks->viewRenderHooks.getOriginalRender2dEffectsPreHud()(thisptr, viewSetup);
}

const csgo::DemoPlaybackParameters* GlobalContext::getDemoPlaybackParametersHook(csgo::EnginePOD* thisptr, ReturnAddress returnAddress)
{
    const auto params = hooks->engineHooks.getOriginalGetDemoPlaybackParameters()(thisptr);

    if (params)
        return features->misc.getDemoPlaybackParametersHook(returnAddress, *params);

    return params;
}

bool GlobalContext::dispatchUserMessageHook(csgo::ClientPOD* thisptr, csgo::UserMessageType type, int passthroughFlags, int size, const void* data)
{
    features->misc.dispatchUserMessageHook(type, size, data);
    if (type == csgo::UserMessageType::Text)
        features->inventoryChanger.onUserTextMsg(*memory, data, size);

    return hooks->clientHooks.getOriginalDispatchUserMessage()(thisptr, type, passthroughFlags, size, data);
}

bool GlobalContext::isPlayingDemoHook(csgo::EnginePOD* thisptr, ReturnAddress returnAddress, std::uintptr_t frameAddress)
{
    const auto result = hooks->engineHooks.getOriginalIsPlayingDemo()(thisptr);

    if (features->misc.isPlayingDemoHook(returnAddress, frameAddress))
        return true;

    return result;
}

void GlobalContext::updateColorCorrectionWeightsHook(csgo::ClientMode* thisptr)
{
    hooks->clientModeHooks.getOriginalUpdateColorCorrectionWeights()(thisptr);
    features->visuals.updateColorCorrectionWeightsHook();
}

float GlobalContext::getScreenAspectRatioHook(csgo::EnginePOD* thisptr, int width, int height)
{
    if (features->misc.aspectRatio() != 0.0f)
        return features->misc.aspectRatio();
    return hooks->engineHooks.getOriginalGetScreenAspectRatio()(thisptr, width, height);
}

void GlobalContext::renderSmokeOverlayHook(csgo::ViewRender* thisptr, bool preViewModel)
{
    if (!features->visuals.renderSmokeOverlayHook())
        hooks->viewRenderHooks.getOriginalRenderSmokeOverlay()(thisptr, preViewModel);
}

double GlobalContext::getArgAsNumberHook(csgo::PanoramaMarshallHelperPOD* thisptr, void* params, int index, ReturnAddress returnAddress)
{
    const auto result = hooks->panoramaMarshallHelperHooks.getOriginalGetArgAsNumber()(thisptr, params, index);
    features->inventoryChanger.getArgAsNumberHook(static_cast<int>(result), returnAddress);
    return result;
}

const char* GlobalContext::getArgAsStringHook(csgo::PanoramaMarshallHelperPOD* thisptr, void* params, int index, ReturnAddress returnAddress)
{
    const auto result = hooks->panoramaMarshallHelperHooks.getOriginalGetArgAsString()(thisptr, params, index);

    if (result)
        features->inventoryChanger.getArgAsStringHook(*memory, result, returnAddress, params);

    return result;
}

void GlobalContext::setResultIntHook(csgo::PanoramaMarshallHelperPOD* thisptr, void* params, int result, ReturnAddress returnAddress)
{
    result = features->inventoryChanger.setResultIntHook(returnAddress, params, result);
    hooks->panoramaMarshallHelperHooks.getOriginalSetResultInt()(thisptr, params, result);
}

unsigned GlobalContext::getNumArgsHook(csgo::PanoramaMarshallHelperPOD* thisptr, void* params, ReturnAddress returnAddress)
{
    const auto result = hooks->panoramaMarshallHelperHooks.getOriginalGetNumArgs()(memory->panoramaMarshallHelper, params);
    features->inventoryChanger.getNumArgsHook(thisptr, result, returnAddress, params);
    return result;
}

void GlobalContext::updateInventoryEquippedStateHook(csgo::InventoryManagerPOD* thisptr, std::uintptr_t inventory, csgo::ItemId itemID, csgo::Team team, int slot, bool swap)
{
    features->inventoryChanger.onItemEquip(team, slot, itemID);
    hooks->inventoryManagerHooks.getOriginalUpdateInventoryEquippedState()(thisptr, inventory, itemID, team, slot, swap);
}

void GlobalContext::soUpdatedHook(csgo::CSPlayerInventoryPOD* thisptr, csgo::SOID owner, csgo::SharedObjectPOD* object, int event)
{
    features->inventoryChanger.onSoUpdated(csgo::SharedObject::from(retSpoofGadgets->client, object));
    hooks->playerInventoryHooks.getOriginalSoUpdated()(thisptr, owner, object, event);
}

int GlobalContext::listLeavesInBoxHook(void* thisptr, const csgo::Vector& mins, const csgo::Vector& maxs, unsigned short* list, int listMax, ReturnAddress returnAddress, std::uintptr_t frameAddress)
{
    if (const auto newVectors = features->misc.listLeavesInBoxHook(returnAddress, frameAddress))
        return hooks->bspQueryHooks.getOriginalListLeavesInBox()(getEngineInterfaces().getEngine().getBSPTreeQuery(), &newVectors->first, &newVectors->second, list, listMax);
    return hooks->bspQueryHooks.getOriginalListLeavesInBox()(getEngineInterfaces().getEngine().getBSPTreeQuery(), &mins, &maxs, list, listMax);
}

#if IS_WIN32()
LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

void* GlobalContext::allocKeyValuesMemoryHook(csgo::KeyValuesSystemPOD* thisptr, int size, ReturnAddress returnAddress)
{
    if (returnAddress == memory->keyValuesAllocEngine || returnAddress == memory->keyValuesAllocClient)
        return nullptr;
    return hooks->keyValuesSystemHooks.getOriginalAllocKeyValuesMemory()(thisptr, size);
}

LRESULT GlobalContext::wndProcHook(HWND window, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (state == GlobalContext::State::Initialized) {
        ImGui_ImplWin32_WndProcHandler(window, msg, wParam, lParam);
        getOtherInterfaces().getInputSystem().enableInput(!gui->isOpen());
    } else if (state == GlobalContext::State::NotInitialized) {
        state = GlobalContext::State::Initializing;

        const windows_platform::DynamicLibrary clientDLL{ windows_platform::DynamicLibraryWrapper{}, csgo::CLIENT_DLL };
        clientInterfaces = createClientInterfacesPODs(InterfaceFinderWithLog{ InterfaceFinder{ clientDLL.getView(), retSpoofGadgets->client } });
        const windows_platform::DynamicLibrary engineDLL{ windows_platform::DynamicLibraryWrapper{}, csgo::ENGINE_DLL };
        engineInterfacesPODs = createEngineInterfacesPODs(InterfaceFinderWithLog{ InterfaceFinder{ engineDLL.getView(), retSpoofGadgets->client } });
        interfaces.emplace();

        PatternNotFoundHandler patternNotFoundHandler;
        const PatternFinder clientPatternFinder{ getCodeSection(clientDLL.getView()), patternNotFoundHandler };
        const PatternFinder enginePatternFinder{ getCodeSection(engineDLL.getView()), patternNotFoundHandler };

        memory.emplace(clientPatternFinder, enginePatternFinder, clientInterfaces->client, *retSpoofGadgets);

        Netvars::init(ClientInterfaces{ retSpoofGadgets->client, *clientInterfaces }.getClient());
        gameEventListener.emplace(getEngineInterfaces().getGameEventManager(memory->getEventDescriptor));

        ImGui::CreateContext();
        ImGui_ImplWin32_Init(window);

        randomGenerator.emplace();
        features.emplace(createFeatures(*memory, ClientInterfaces{ retSpoofGadgets->client, *clientInterfaces }, getEngineInterfaces(), getOtherInterfaces(), clientPatternFinder, enginePatternFinder, *randomGenerator));
        config.emplace(features->misc, features->inventoryChanger, features->glow, features->backtrack, features->visuals, getOtherInterfaces(), *memory);
        gui.emplace();
        hooks->install(clientInterfaces->client, getEngineInterfaces(), getOtherInterfaces(), *memory);

        state = GlobalContext::State::Initialized;
    }

    return CallWindowProcW(hooks->originalWndProc, window, msg, wParam, lParam);
}

HRESULT GlobalContext::presentHook(IDirect3DDevice9* device, const RECT* src, const RECT* dest, HWND windowOverride, const RGNDATA* dirtyRegion)
{
    [[maybe_unused]] static bool imguiInit{ ImGui_ImplDX9_Init(device) };

    if (config->loadScheduledFonts())
        ImGui_ImplDX9_DestroyFontsTexture();

    ImGui_ImplDX9_NewFrame();
    ImGui_ImplWin32_NewFrame();

    renderFrame();

    if (device->BeginScene() == D3D_OK) {
        ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
        device->EndScene();
    }

    //
    GameData::clearUnusedAvatars();
    features->inventoryChanger.clearUnusedItemIconTextures();
    //

    return hooks->originalPresent(device, src, dest, windowOverride, dirtyRegion);
}

HRESULT GlobalContext::resetHook(IDirect3DDevice9* device, D3DPRESENT_PARAMETERS* params)
{
    ImGui_ImplDX9_InvalidateDeviceObjects();
    features->inventoryChanger.clearItemIconTextures();
    GameData::clearTextures();
    return hooks->originalReset(device, params);
}

#else
int GlobalContext::pollEventHook(SDL_Event* event)
{
    const auto result = hooks->pollEvent(event);

    if (state == GlobalContext::State::Initialized) {
        if (result && ImGui_ImplSDL2_ProcessEvent(event) && gui->isOpen())
            event->type = 0;
    } else if (state == GlobalContext::State::NotInitialized) {
        state = GlobalContext::State::Initializing;

        const linux_platform::SharedObject clientSo{ linux_platform::DynamicLibraryWrapper{}, csgo::CLIENT_DLL };
        clientInterfaces = createClientInterfacesPODs(InterfaceFinderWithLog{ InterfaceFinder{ clientSo.getView(), retSpoofGadgets->client } });
        const linux_platform::SharedObject engineSo{ linux_platform::DynamicLibraryWrapper{}, csgo::ENGINE_DLL };
        engineInterfacesPODs = createEngineInterfacesPODs(InterfaceFinderWithLog{ InterfaceFinder{ engineSo.getView(), retSpoofGadgets->client } });

        interfaces.emplace();
        PatternNotFoundHandler patternNotFoundHandler;
        const PatternFinder clientPatternFinder{ linux_platform::getCodeSection(clientSo.getView()), patternNotFoundHandler };
        const PatternFinder enginePatternFinder{ linux_platform::getCodeSection(engineSo.getView()), patternNotFoundHandler };

        memory.emplace(clientPatternFinder, enginePatternFinder, clientInterfaces->client, *retSpoofGadgets);

        Netvars::init(ClientInterfaces{ retSpoofGadgets->client, *clientInterfaces }.getClient());
        gameEventListener.emplace(getEngineInterfaces().getGameEventManager(memory->getEventDescriptor));

        ImGui::CreateContext();

        randomGenerator.emplace();
        features.emplace(createFeatures(*memory, ClientInterfaces{ retSpoofGadgets->client, *clientInterfaces }, getEngineInterfaces(), getOtherInterfaces(), clientPatternFinder, enginePatternFinder, *randomGenerator));
        config.emplace(features->misc, features->inventoryChanger, features->glow, features->backtrack, features->visuals, getOtherInterfaces(), *memory);
        
        gui.emplace();
        hooks->install(clientInterfaces->client, getEngineInterfaces(), getOtherInterfaces(), *memory);

        state = GlobalContext::State::Initialized;
    }
    
    return result;
}

void GlobalContext::swapWindowHook(SDL_Window* window)
{
    [[maybe_unused]] static const auto _ = ImGui_ImplSDL2_InitForOpenGL(window, nullptr);

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame(window);

    renderFrame();

    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    GameData::clearUnusedAvatars();
    features->inventoryChanger.clearUnusedItemIconTextures();

    hooks->swapWindow(window);
}

#endif

void GlobalContext::viewModelSequenceNetvarHook(csgo::recvProxyData* data, void* outStruct, void* arg3)
{
    const auto viewModel = csgo::Entity::from(retSpoofGadgets->client, static_cast<csgo::EntityPOD*>(outStruct));

    if (localPlayer && ClientInterfaces{ retSpoofGadgets->client, *clientInterfaces }.getEntityList().getEntityFromHandle(viewModel.owner()) == localPlayer.get().getPOD()) {
        if (const auto weapon = csgo::Entity::from(retSpoofGadgets->client, ClientInterfaces{ retSpoofGadgets->client, *clientInterfaces }.getEntityList().getEntityFromHandle(viewModel.weapon())); weapon.getPOD() != nullptr) {
            if (features->visuals.isDeagleSpinnerOn() && weapon.getNetworkable().getClientClass()->classId == ClassId::Deagle && data->value._int == 7)
                data->value._int = 8;

            features->inventoryChanger.fixKnifeAnimation(weapon, data->value._int, *randomGenerator);
        }
    }

    proxyHooks.viewModelSequence.originalProxy(data, outStruct, arg3);
}

void GlobalContext::spottedHook(csgo::recvProxyData* data, void* outStruct, void* arg3)
{
    if (features->misc.isRadarHackOn()) {
        data->value._int = 1;

        if (localPlayer) {
            const auto entity = csgo::Entity::from(retSpoofGadgets->client, static_cast<csgo::EntityPOD*>(outStruct));
            if (const auto index = localPlayer.get().getNetworkable().index(); index > 0 && index <= 32)
                entity.spottedByMask() |= 1 << (index - 1);
        }
    }

    proxyHooks.spotted.originalProxy(data, outStruct, arg3);
}

void GlobalContext::fireGameEventCallback(csgo::GameEventPOD* eventPointer)
{
    const auto event = csgo::GameEvent::from(retSpoofGadgets->client, eventPointer);

    switch (fnv::hashRuntime(event.getName())) {
    case fnv::hash(csgo::round_start):
        GameData::clearProjectileList();
        features->misc.preserveKillfeed(true);
        [[fallthrough]];
    case fnv::hash(csgo::round_freeze_end):
        features->misc.purchaseList(getEngineInterfaces().getEngine(), &event);
        break;
    case fnv::hash(csgo::player_death):
        features->inventoryChanger.updateStatTrak(event);
        features->inventoryChanger.overrideHudIcon(*memory, event);
        features->misc.killMessage(getEngineInterfaces().getEngine(), event);
        features->misc.killSound(getEngineInterfaces().getEngine(), event);
        break;
    case fnv::hash(csgo::player_hurt):
        features->misc.playHitSound(getEngineInterfaces().getEngine(), event);
        features->visuals.hitEffect(&event);
        features->visuals.hitMarker(&event);
        break;
    case fnv::hash(csgo::vote_cast):
        features->misc.voteRevealer(event);
        break;
    case fnv::hash(csgo::round_mvp):
        features->inventoryChanger.onRoundMVP(event);
        break;
    case fnv::hash(csgo::item_purchase):
        features->misc.purchaseList(getEngineInterfaces().getEngine(), &event);
        break;
    case fnv::hash(csgo::bullet_impact):
        features->visuals.bulletTracer(event);
        break;
    }
}

void GlobalContext::renderFrame()
{
    ImGui::NewFrame();

    if (const auto& displaySize = ImGui::GetIO().DisplaySize; displaySize.x > 0.0f && displaySize.y > 0.0f) {
        StreamProofESP::render(*memory, *config);
        features->misc.purchaseList(getEngineInterfaces().getEngine());
        features->misc.noscopeCrosshair(ImGui::GetBackgroundDrawList());
        features->misc.recoilCrosshair(ImGui::GetBackgroundDrawList());
        features->misc.drawOffscreenEnemies(getEngineInterfaces().getEngine(), ImGui::GetBackgroundDrawList());
        features->misc.drawBombTimer();
        features->misc.spectatorList();
        features->visuals.hitMarker(nullptr, ImGui::GetBackgroundDrawList());
        features->visuals.drawMolotovHull(ImGui::GetBackgroundDrawList());
        features->misc.watermark();

        features->aimbot.updateInput(*config);
        features->visuals.updateInput();
        StreamProofESP::updateInput(*config);
        features->misc.updateInput();
        Triggerbot::updateInput(*config);
        Chams::updateInput(*config);
        features->glow.updateInput();

        gui->handleToggle(features->misc, getOtherInterfaces());

        if (gui->isOpen())
            gui->render(features->misc, features->inventoryChanger, features->glow, features->backtrack, features->visuals, getEngineInterfaces(), ClientInterfaces{ retSpoofGadgets->client, *clientInterfaces }, getOtherInterfaces(), *memory, *config);
    }

    ImGui::EndFrame();
    ImGui::Render();
}
