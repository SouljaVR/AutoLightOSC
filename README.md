# AutoLightOSC

AutoLightOSC is a Windows application that captures screen colors and sends them to VRChat via OSC (Open Sound Control). Perfect for syncing your avatar's lighting with your VRChat stream view/world, video, or any other content on your screen.

![image](https://github.com/SouljaVR/AutoLightOSC/blob/master/demo.gif?raw=true)

## Features

- **Optimized Screen Capture:** Uses the Windows Desktop Duplication API for optimized capture. Auto-detects VRChat, and other applications can be manually chosen.
- **Spout2 Integration:** Receive frames from any Spout-compatible sender, VRChat included.
- **Color Processing:**
  - Force max brightness/luminance
  - Saturation adjustment
  - White Mix
  - Temporal smoothing
- **Debug View:** See what's being captured and crop to specific areas
- **Performance Tuning:** Adjustable capture FPS and OSC message rate
- **Window Management:** Ability to keep target window on top
- **Optimized:** Full C/C++, Negligible CPU/GPU/RAM use, practically none when used as intended (low capture FPS + smoothing). Even with high FPS the usage is incredibly low.
- **And more:** Options to change the OSC port, parameter names etc.

## Requirements

- Windows 10/11
- DirectX 11 capable GPU (If you can play VRChat you are probably fine)
- VRChat with OSC enabled
- Unity/Avatar Creation/OSC integration competence, I do not provide a prefab for this.

## Installation

1. Download the latest release from the [Releases](https://github.com/SouljaVR/AutoLightOSC/releases) page
2. Run the `AutoLightOSC.exe` - No installation is required - the application is portable.

## Usage

### Quick Start

1. Launch AutoLightOSC
2. The app will automatically try to detect VRChat
3. Click "Start Capture"

### Controls

- **Start/Stop Capture:** Begin or end the color capture process.
- **OSC Rate:** How many OSC messages to send per second (1-240).
- **FPS:** Screen capture rate (1-60) - Recommended is something really low like 5fps, but if you want the lighting to be low latency you can put it higher. *VRChat floats update at around 10hz so there isnt much benefit going over 5fps + smoothing.*
- **Use Spout2:** Enable to receive frames from Spout2 senders instead of screen capture.
- **White Mix:** Blend the captured color with white (0-100%) - basically the same as lowering saturation, this setting is pretty redundant but I left it there anyway in case it has a usecase I havent thought of lol.
- **Saturation Boost:** Adjust color saturation (-100% to +100%).
- **Force Max Brightness:** Always use the brightest possible version of the current color, recommended to keep this on if you want your avatar to have the highest influence possible by this system.
- **Enable Smoothing:** Smooth color transitions, recommended to keep on so the colour changes are gradual on the avatar. If you use avatar parameter smoothing for the feature you are controlling with this, this is not needed. Smoothing is fixed to 60fps, independent of the capture FPS, and it influences the OSC output rate. Keep this in mind and ensure the OSC rate is set to something sensible so you arent overloading VRChat with a crazy high send rate. 3 parameters send each poll, so the rate is 3x whatever it says. E.g an OSC rate of 3 is 9 messages per second. 
- **Smoothing Rate:** How quickly colors blend (higher = slower transitions)
- **Keep Target On Top:** Forces the capture window to stay on top so other windows do not get in the way.

### Debug View

Click "Show Options" to access:
- RGB and OSC value display
- Target application selection
- Preview of what's being captured
- Ability to crop the capture area (click and drag on the preview)
- OSC Output settings (VRChat default port is 9000, no need to change this unless you have explicitly changed the default VRChat port, you would know if you have done this.)

## Avatar Setup

For your VRChat avatar to respond to AutoLightOSC, it needs to:

1. Have 3 float parameters, each float representing each colour channel. You can use any parameter names, its customizable.
2. Be configured to use these parameters for influence. Whatever you do with them is up to you.

VRChat will receive float values for each colour channel. The RGB values are mapped from int 0-255 to float values (-1 to 1). So RGB 255,0,0 (Red) would give this output:

```
AL_Red: 1.0
AL_Green: -1.0
AL_Blue: -1.0
```

I do not provide a prefab for this. It is up to you to utilize the values, avatar/unity competence is needed. An example usecase would be animating the colour property of a shader. You can individually keyframe the R-G-B values as the keyframes are generally split in most shaders. If they arent, you can mix keyframes using a blendtree to set a colour field to specific colours from 3 different inputs (R/G/B), even if it is just a singular colour entry. I personally use this to control the overall tint of my avatar so it blends into worlds better. Unlike normal shader lighting, it adapts properly to different areas of the world and dynamic elements of the world. It works best when blended with shader based ambient lighting.

## Configuration

Settings are automatically saved here:
`%APPDATA%\AutoLightOSC\settings.json`

## License

This project is licensed under the GNU General Public License v3.0 - see the LICENSE.txt file for details.

Copyright (c) 2025 BigSoulja/SouljaVR
Developed and maintained by BigSoulja/SouljaVR and all direct or indirect
contributors to the GitHub repository.
This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

## Acknowledgments

- [Dear ImGui](https://github.com/ocornut/imgui) - Used for the user interface
- [Spout2](https://github.com/leadedge/Spout2) - Used for frame sharing between applications
- All contributors and supporters of the project

## Support and Contributions

This project is open source, but continued development and maintenance benefit from your support.

- Discord: @bigsoulja
- Website: [www.soulja.io](https://www.soulja.io)
- GitHub: [SouljaVR/AutoLightOSC](https://github.com/SouljaVR/AutoLightOSC)

Businesses and collaborators: support via funding, sponsoring, or integration opportunities is welcome.
