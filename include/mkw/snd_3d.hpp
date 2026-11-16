#pragma once
// MKW's 3D sound model (Audio::Engine3D::UpdateAmbientParam, USA 806f296c,
// over NW4R's Sound3DCalc), for one listener; stereo, surround or mono. Platform
// free; the race audio layer runs it on the audio thread for every sound an
// actor (kart, item, object) plays. docs/AUDIO_SOURCE.md "3D".
#include <mkw/snd_engine.hpp>

namespace mkw::snd {
struct Vec3f { f32 x = 0, y = 0, z = 0; };

// Sound3DListener: world -> listener matrix (rows; listener looks down -z),
// its position and per-game-frame velocity, and EchoMgr's listener volume.
struct Listener3D {
 f32 m[3][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}};
 Vec3f position, velocity;
 f32 volume = 1, fxSend = 0;
 // RaceCamera::GetViewMtx (8059bc20): the listener sits `scale` of the way
 // from the eye to the target, looking at the target (C_MTXLookAt).
 void lookAt(Vec3f eye, Vec3f target, Vec3f up, f32 scale);
};

// Sound3DActor parameters copied into each sound's ambient argument.
// userParam (RaceActor::UpdateUserParam*): bits 0-3 index the distance
// table for full volume, bits 4-7 the interior size, 0x100 no Doppler,
// 0x200 no surround, 0x400 (with sound 0x11b) skip the listener volume.
struct Actor3D { Vec3f position, velocity; u32 userParam = 0; };

// Per sound and listener: the last values, for the 0.1-per-update limit.
struct Sound3DHistory { bool primed = false; f32 volume = 0, pan = 0, surround = 0; };

struct Engine3D {
 // Audio::Manager (8070f4a0) and EGG::ExpAudioMgr's 3D settings.
 int maxPriorityReduction = 32;
 f32 unitDistance = 2000.0f, panRange = 0.9f, sonicVelocity = 566.6667f;
 bool swapPan = false;
 // The engine's output mode (Engine3D +4): surround/DPL2 use CalcPanSurround,
 // stereo CalcPanStereo, mono leaves pan and span centred.
 OutputMode speakerMode = OutputStereo;
 static constexpr f32 Distance[16] = {0, 500, 1000, 1500, 2000, 2500, 3000, 3500, 4000, 4500, 5000, 5500, 6000, 6500, 7000, 30000};
 // The ambient parameters for one sound (Sound3DEngine flags from the sound's
 // 3D parameters: bits set there are not 3D controlled; Doppler needs a factor).
 Ambient update(const SoundEntry& sound, u32 soundId, const Actor3D& actor, const Listener3D& listener, Sound3DHistory* history) const;
 // Sound3DCalc pieces (exposed for tests).
 static void angleAndDistance(Vec3f rel, f32 distance, f32 interior, f32& angle, f32& distanceNrm);
 f32 panStereo(f32 angle, f32 distanceNrm) const;
 void panSurround(f32 angle, f32 distanceNrm, f32& pan, f32& span) const;
 f32 doppler(f32 factor, Vec3f rel, Vec3f actorVelocity, Vec3f listenerVelocity) const;
};
}
