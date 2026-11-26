// OpenFIREScopeClient.h - the connection of an Unreal Engine 5 game to the OpenFIRE Scope Daemon.
//
// Plain C++ (no UObject), on Unreal's own sockets (modules "Sockets" and "Networking"). One client is shared by all
// the OpenFIREScopeComponent of the game; use it directly only for your own setups.
//
// MIT License - Copyright (c) 2026 Alessandro Satanassi, Team OpenFIRE
#pragma once

#include "CoreMinimal.h"

class FSocket;
class FInternetAddr;

class FOpenFIREScopeClient
{
public:
	static constexpr int32 Width = 240;                 // the scope image is always 240 x 240
	static constexpr int32 Height = 240;
	static constexpr int32 MaxPlayers = 4;              // players 1..4
	static constexpr int32 DefaultDiscoveryPort = 47810;

	// How the image is laid out in memory. FColor arrays (ReadPixels) are BGRX8888.
	enum class EPixels : uint8
	{
		RGB888 = 0,     // 3 bytes R, G, B
		RGB565 = 1,     // 2 bytes, 16-bit little-endian, R in the 5 high bits
		BGRX8888 = 2,   // 4 bytes B, G, R, X: FColor
	};

	// The client shared by the whole game, created on first use and closed when the last user releases it.
	static TSharedRef<FOpenFIREScopeClient> Acquire();

	// Opens a non-blocking UDP socket on 127.0.0.1 and sends the first discovery request.
	explicit FOpenFIREScopeClient(int32 DiscoveryPort = DefaultDiscoveryPort);
	// Closes the socket: the scopes show NO SIGNAL (or the daemon's screen capture) about 0.5 s later.
	~FOpenFIREScopeClient();

	// Once per frame (more calls in the same frame do nothing). Never blocks: reads the daemon's answers, asks again
	// once a second, sends the pause packet every 200 ms for the paused players.
	void Update();

	// True if player 1..4 has a paired scope right now (or bAlwaysSend is on).
	bool HasScope(int32 Player) const;

	// Sends one 240x240 image, top row first (bFlipY: bottom row first). Stride = bytes from one row to the next,
	// 0 = tightly packed. Returns false if nothing was sent (no scope, bad arguments). Ends the pause of that player.
	bool SendFrame(int32 Player, const void* Pixels, EPixels Format, int32 Stride = 0, bool bFlipY = false);

	// Pause menu for one player 1..4, or all with 0: the scope keeps the last image. The next SendFrame() ends it.
	void SetPaused(int32 Player, bool bPaused);

	// A new random session for a brand-new match (optional; the constructor does it).
	void NewSession();

	bool IsOpen() const { return Socket != nullptr; }
	bool IsDaemonSeen() const { return bDaemonSeen; }
	int32 GetPort(int32 Player) const { return Player >= 1 && Player <= MaxPlayers ? Ports[Player] : 0; }

	// Testing only: send to the default ports 47811-47814 even without a scope (daemon's Preview tab).
	bool bAlwaysSend = false;

private:
	void Send(const uint8* Data, int32 Length, int32 Port);
	void WriteHeader(uint8* H, uint8 Format, uint32 Frame, uint8 Strip, int32 Player) const;
	void ParseAnswer(const FString& Text);
	int32 PortOf(int32 Player) const { return Ports[Player] != 0 ? Ports[Player] : DefaultDiscoveryPort + Player; }

	FSocket* Socket = nullptr;
	TSharedPtr<FInternetAddr> Target;
	TSharedPtr<FInternetAddr> From;
	int32 DiscoveryPort = DefaultDiscoveryPort;
	uint32 Session = 0;
	int32 Ports[MaxPlayers + 1] = {};
	uint32 Frame[MaxPlayers + 1] = {};
	uint8 Format[MaxPlayers + 1] = {};
	bool bPausedPlayer[MaxPlayers + 1] = {};
	double NextPresence[MaxPlayers + 1] = {};
	double NextWhoami = 0.0;
	double LastAnswer = 0.0;
	bool bDaemonSeen = false;
	uint64 LastUpdateFrame = MAX_uint64;
	TArray<uint8> Packet;
};
