# J.E.S.T.E.R — Holographic Interface

A full-screen holographic desktop assistant that combines gesture control, voice interaction, and a 3D media deck. The app is designed to feel like a cinematic, hands-in-the-air control surface: your hands do the navigating, your voice does the commands, and the display behaves like a sci-fi control room.

## What is in the app now

- Fullscreen holographic desktop shell using Electron
- Hidden background environment and minimal UI clutter for a clean overlay feel
- Real-time hand tracking with MediaPipe
- Dwell-based typing that feels closer to real typing than pinch typing
- One-second key dwell threshold for actual letter activation
- All-finger typing mode for natural keyboard use
- Two-finger keyboard close gesture to dismiss the floating keyboard
- Right-arm swipe gesture to move backward through recent UI state
- Stack-based back navigation returning to the previous screen and eventually home
- Connect Files deck with media browsing, inspect mode, rotation, zoom, and pan
- Open-file inspection with multi-hand zoom and rotation gestures
- Compact hologram keyboard for text entry and file naming
- Voice-driven JESTER actions and system controls

## Core interaction model

### Gesture controls

- Move, grab, and manipulate holograms with tracked hand motion
- Use a pinch or extended hand motion to interact with scene objects
- Typing is built around dwell timing rather than pinch detection
- Hold a finger over a key for about 1 second to register the input
- All fingers can be used to type in the active keyboard mode
- Two-finger close gesture dismisses the keyboard and clears the overlay state
- Right-hand swipe to the left steps back through prior events
- When the history is exhausted, the app returns home

### Keyboard behavior

- Keyboard appears as a holographic floating panel
- Typing is activated by a deliberate hand pose and dwell timing
- Keys trigger after the dwell threshold, giving a more realistic “press” feel
- Close actions can be triggered by a two-finger gesture or the built-in close control
- The typing experience is tuned to feel closer to real world typing than a pinch-based system

### Connect Files

- Open a folder of images and videos
- Browse media as a floating deck of cards
- Focus a selected item and open it into a larger presentation view
- Use multi-hand motion to rotate and zoom an inspected image
- Pan the object with one open hand while it is active
- Back out step-by-step from the open item to deck view and then to the home state

## Desktop and app flow

### Start the server

```bash
cd /Users/sudharsans/Virtual/jester
npm install
npm start
```

### Launch the fullscreen Electron app

```bash
cd /Users/sudharsans/Virtual/jester
npm run app
```

This starts the desktop overlay and brings up the app in fullscreen mode, where the holographic interface can sit over the live desktop.

## Features summary

### Voice commands

- Spawn holograms and scene elements
- Dismiss, reset, rotate, and scale objects
- Trigger JESTER responses and system actions
- Launch or close desktop apps
- Control media, volume, and fullscreen state
- Open URLs and run web searches
- Enter or exit desktop mainframe mode

### Holographic deck and visual system

- Minimal interface with hidden environment clutter
- Clean orbital, futuristic visual presentation
- layered overlays and floating UI elements instead of heavy background props
- Visual HUD and status feedback for gesture state and keyboard activity

## Architecture

- Hand tracking: MediaPipe
- Rendering: three.js
- Desktop shell: Electron
- Server layer: Node + Express
- WebSocket relay: real-time phone/display hand share and sync
- Voice layer: OpenAI transcription and speech

## Tips

- Run the app from the project folder, not from the parent directory
- If a port is already busy, stop the existing server before starting again
- For voice features, configure the OpenAI environment variables before use
- Use the Electron app for the full desktop overlay experience

## Notes

This project is a large single-scene holographic interface with a voice-driven assistant and hand-tracked controls built specifically for immersive, screen-overlay operation. It is tuned to feel closer to natural physical interaction than purely touch-based controls, especially around typing and file inspection.
