#pragma once
#import "ESP.h"

// Camera.Render(this) is called once per camera per frame by Unity's own render
// pipeline, after all of that frame's Update/LateUpdate calls have already positioned
// the camera. Writing the aim direction here, right before calling the original,
// guarantees our write is the last one before this frame renders.
//
// The previous approach wrote game_sdk->set_forward from get_players(), which runs on
// its own separate CADisplayLink loop - that races the game's own per-frame camera
// update (which reads touch input and re-applies the player's look rotation every
// frame) with no guaranteed ordering, so our write was frequently overwritten before
// render and Aim Head visibly did nothing.
static void (*orig_Camera_Render)(void *camera);

inline void hooked_Camera_Render(void *camera) {
    if (Vars.Enable && Vars.AimHead && camera) {
        Vector3 headWorld;
        if (FindAimHeadTarget(camera, headWorld)) {
            void *camTransform = game_sdk->Component_GetTransform(camera);
            if (camTransform) {
                Vector3 camPos = game_sdk->get_position(camTransform);
                Vector3 dir = Vector3::Normalized(headWorld - camPos);
                game_sdk->set_forward(camTransform, dir);
            }
        }
    }
    orig_Camera_Render(camera);
}

inline void installAimHook() {
    HOOK(0x915ECE4, hooked_Camera_Render, orig_Camera_Render);
}
