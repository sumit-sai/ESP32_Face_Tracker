# RoboEyes integration: automatic behaviour test

The user confirmed that the first display/camera/servo coexistence test works.
This milestone adds movement-driven gaze and face-driven expressions. Serial
mood commands are replaced by automatic moods. The standalone RoboEyesESP32Test
sketch is preserved. The user compiled and uploaded through Arduino IDE and
confirmed the integration works after reversing the horizontal eye mapping;
the vertical mapping was already correct. The steps below are a repeatable
test checklist, not a claim that every edge case was individually verified.

Install Adafruit SSD1306, Adafruit GFX Library, and FluxGarage RoboEyes through
Arduino Library Manager. Local versions are ESP32 core 3.3.11 and RoboEyes 1.1.1.
Use ESP32S3 Dev Module, 8 MB flash, default 8 MB partition, and OPI PSRAM.

OLED: 128x64 SSD1306 at 0x3C, SDA GPIO 1, SCL GPIO 2, VCC 3.3V, GND common.
The installed camera driver uses I2C controller 1; the OLED uses Wire/controller 0.

1. Upload PCFaceTrack and open Serial Monitor at 115200 baud. Uploading replaces
   the running firmware. Servos receive the existing 90-degree startup command.
2. Confirm `OLED ready` and neutral, centered, blinking eyes before any face is
   detected. Random idle gaze is disabled.
3. Connect to RobotArm-Face and open http://192.168.4.1/status and
   http://192.168.4.1:81/frame. Check that status and an image load while blinking
   continues. HTTP handling may cause brief animation pauses under slow clients.
4. Run the existing PC tracker with the normal servo power arrangement. Show a
   face: the expression should become HAPPY. Move left/right, up/down, and
   diagonally: gaze should follow the commanded servo movement. Hold still:
   each gaze axis returns toward center after 300 ms without movement.
5. Leave the view: one second after the last detected face's capture timestamp,
   the eyes should become TIRED (the initial sad substitute). Brief detection
   gaps should not change the mood. Return: the eyes should become HAPPY again.
6. During search sweeps, eyes should follow movement while keeping the sad mood.
   Stop the PC tracker after detecting a face: stale data should not leave the
   expression happy forever. Restart and confirm recovery.

Gaze uses changes in commanded servo angles, not measured physical feedback.
In eyes.cpp, PAN_TO_SCREEN and TILT_TO_SCREEN determine display direction;
negate the relevant value if a physical direction test shows that axis reversed.
CENTER_DELAY_MS and SAD_DELAY_MS control the two waiting periods without delay().

The display runs at a maximum of 30 FPS in the main loop; the existing servo
task is unchanged. Missing OLED/initialization failure is logged and leaves
the robot running without eyes. Repeat the hardware checks after future changes.
