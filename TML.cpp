#include "GameInput.h"
#include <stdio.h>
#include <stdbool.h> // Include for bool type
#include <cmath> // Include for fabs

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#define Sleep(ms) usleep((ms)*1000)
#endif

struct Joysticks
{
    uint32_t deviceCount;
    IGameInputDevice** devices;
};

void CALLBACK deviceChangeCallback(GameInputCallbackToken callbackToken, void* context, IGameInputDevice* device, uint64_t timestamp, GameInputDeviceStatus currentStatus, GameInputDeviceStatus previousStatus)
{
    Joysticks* joysticks = (Joysticks*)context;
    if (currentStatus & GameInputDeviceConnected)
    {
        for (uint32_t i = 0; i < joysticks->deviceCount; ++i)
        {
            if (joysticks->devices[i] == device) return;
        }
        ++joysticks->deviceCount;
        joysticks->devices = (IGameInputDevice**)realloc(joysticks->devices, joysticks->deviceCount * sizeof(IGameInputDevice*));
        joysticks->devices[joysticks->deviceCount - 1] = device;
    }
}

int main()
{
    Joysticks joysticks = { 0 };
    IGameInput* input;
    if (FAILED(GameInputCreate(&input))) {
        fprintf(stderr, "GameInputCreate failed\n");
        return 1;
    }

    IGameInputDispatcher* dispatcher;
    if (FAILED(input->CreateDispatcher(&dispatcher))) {
        fprintf(stderr, "CreateDispatcher failed\n");
        return 1;
    }

    GameInputCallbackToken callbackId;
    if (FAILED(input->RegisterDeviceCallback(0, GameInputKindController, GameInputDeviceAnyStatus, GameInputBlockingEnumeration, &joysticks, deviceChangeCallback, &callbackId))) {
        fprintf(stderr, "RegisterDeviceCallback failed\n");
        return 1;
    }

    bool buttons[64];
    GameInputSwitchPosition switches[64];
    float axes[64];
    float prevAxes[64] = { 0 }; // Store previous axis values
    bool hasMoved = false;

    while (1)
    {
        dispatcher->Dispatch(0);

        for (uint32_t i = 0; i < joysticks.deviceCount; ++i)
        {
            IGameInputReading* reading;
            if (SUCCEEDED(input->GetCurrentReading(GameInputKindController, joysticks.devices[i], &reading)))
            {
                reading->GetControllerAxisState(ARRAYSIZE(axes), axes);
                reading->GetControllerSwitchState(ARRAYSIZE(switches), switches);
                reading->GetControllerButtonState(ARRAYSIZE(buttons), buttons);

                hasMoved = false;
                for (uint32_t j = 0; j < reading->GetControllerAxisCount(); ++j) {
                    if (std::fabs(axes[j] - prevAxes[j]) > 0.01f) { // Check for significant change (adjust threshold as needed)
                        hasMoved = true;
                        break;
                    }
                }
                for (uint32_t j = 0; j < reading->GetControllerSwitchCount(); ++j) {
                    if (std::fabs(axes[j] - prevAxes[j]) > 0.01f) { // Check for significant change (adjust threshold as needed)
                        hasMoved = true;
                        break;
                    }
                }
                for (uint32_t j = 0; j < reading->GetControllerButtonCount(); ++j) {
                    if (buttons[j]) {
                        printf("Joystick %d, ", i);
                        printf("Axes - ");
                        for (uint32_t j = 0; j < reading->GetControllerAxisCount(); ++j) {
                            printf("%d:%f ", j, axes[j]);
                            prevAxes[j] = axes[j]; // Update previous axis values
                        }
                        printf("Switches - ");
                        for (uint32_t j = 0; j < reading->GetControllerSwitchCount(); ++j) {
                            printf("%d:%d ", j, switches[j]);
                        }
                        printf("Buttons - ");
                        for (uint32_t j = 0; j < reading->GetControllerButtonCount(); ++j) {
                            if (buttons[j]) printf("%d ", j);
                        }
                        puts("");
                    }
                }

                if (hasMoved)
                { // Only print if axes moved or buttons pressed
                    printf("Joystick %d, ", i);
                    printf("Axes - ");
                    for (uint32_t j = 0; j < reading->GetControllerAxisCount(); ++j) {
                        printf("%d:%f ", j, axes[j]);
                        prevAxes[j] = axes[j]; // Update previous axis values
                    }
                    printf("Switches - ");
                    for (uint32_t j = 0; j < reading->GetControllerSwitchCount(); ++j) {
                        printf("%d:%d ", j, switches[j]);
                    }
                    printf("Buttons - ");
                    for (uint32_t j = 0; j < reading->GetControllerButtonCount(); ++j) {
                        if (buttons[j]) printf("%d ", j);
                    }
                    puts("");
                }
                reading->Release();
            }
        }
        Sleep(30); // Reduced sleep time for responsiveness
    }

    input->Release(); // Release the input object before exiting
    dispatcher->Release(); // Release the dispatcher object
    return 0;
}