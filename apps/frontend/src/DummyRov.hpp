#pragma once

#include "Pilot.hpp"

// A pretend ROV that flies on the pilot's output, for trying the controls with
// no vehicle attached. Seen from above in a pool, with its depth and every
// output beside it.
struct DummyRov {
    float x = 0.0f;        // metres from the pool's centre, +x right
    float y = 0.0f;        // +y towards the top of the view
    float heading = 0.0f;  // radians clockwise from the top of the view
    float depth = 1.0f;    // metres

    // Speeds lag the commands a little, so it does not stop dead.
    float surgeSpeed = 0.0f;
    float swaySpeed = 0.0f;
    float heaveSpeed = 0.0f;
    float yawSpeed = 0.0f;
};

void drawDummyRov(DummyRov& rov, const PilotOutput& out, bool* open);
