// OpenFIREScopeClient.cpp - see OpenFIREScopeClient.h.
//
// MIT License - Copyright (c) 2026 Alessandro Satanassi, Team OpenFIRE
#include "OpenFIREScopeClient.h"

#include "CoreGlobals.h"
#include "HAL/PlatformTime.h"
#include "IPAddress.h"
#include "Misc/Guid.h"
#include "SocketSubsystem.h"
#include "Sockets.h"

namespace
{
	constexpr int32 Strips = 15;
	constexpr int32 StripRows = 16;
	constexpr int32 HeaderSize = 16;
	constexpr double WhoamiEvery = 1.0;      // discovery request, seconds
	constexpr double AnswerTimeout = 3.0;    // no answer for so long: daemon gone, no scopes
	constexpr double PresenceEvery = 0.2;    // pause packet; the daemon forgets a source after 0.5 s

	int32 BytesPerPixel(FOpenFIREScopeClient::EPixels P)
	{
		return P == FOpenFIREScopeClient::EPixels::RGB888 ? 3 : P == FOpenFIREScopeClient::EPixels::RGB565 ? 2 : 4;
	}
}

TSharedRef<FOpenFIREScopeClient> FOpenFIREScopeClient::Acquire()
{
	static TWeakPtr<FOpenFIREScopeClient> Shared;
	TSharedPtr<FOpenFIREScopeClient> Client = Shared.Pin();
	if (!Client.IsValid())
	{
		Client = MakeShared<FOpenFIREScopeClient>();
		Shared = Client;
	}
	return Client.ToSharedRef();
}

FOpenFIREScopeClient::FOpenFIREScopeClient(int32 InDiscoveryPort)
{
	DiscoveryPort = InDiscoveryPort > 0 ? InDiscoveryPort : DefaultDiscoveryPort;
	Packet.SetNumUninitialized(HeaderSize + Width * StripRows * 4);
	for (uint8& F : Format) F = uint8(EPixels::RGB888);

	ISocketSubsystem* Subsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	if (!Subsystem) return;
	FSocket* NewSocket = Subsystem->CreateSocket(NAME_DGram, TEXT("OpenFIRE scope"), FNetworkProtocolTypes::IPv4);
	if (!NewSocket) return;
	TSharedRef<FInternetAddr> Local = Subsystem->CreateInternetAddr(FNetworkProtocolTypes::IPv4);
	Local->SetLoopbackAddress();
	Local->SetPort(0);   // any free port: the answers come back here
	if (!NewSocket->Bind(*Local))
	{
		Subsystem->DestroySocket(NewSocket);
		UE_LOG(LogTemp, Warning, TEXT("OpenFIRE scope: cannot bind a UDP socket, the scopes are off."));
		return;
	}
	NewSocket->SetNonBlocking(true);
	int32 ActualSize = 0;
	NewSocket->SetSendBufferSize(4 * 1024 * 1024, ActualSize);   // 15 datagrams in a row; on macOS also > 9 KB
	Socket = NewSocket;
	Target = Subsystem->CreateInternetAddr(FNetworkProtocolTypes::IPv4);
	Target->SetLoopbackAddress();
	From = Subsystem->CreateInternetAddr(FNetworkProtocolTypes::IPv4);
	NewSession();
	Update();   // first discovery request at once
}

FOpenFIREScopeClient::~FOpenFIREScopeClient()
{
	if (Socket)
	{
		Socket->Close();
		if (ISocketSubsystem* Subsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)) Subsystem->DestroySocket(Socket);
		Socket = nullptr;
	}
}

void FOpenFIREScopeClient::NewSession()
{
	uint32 S = 0;
	while (S == 0 || S == Session)
	{
		const FGuid Guid = FGuid::NewGuid();
		S = Guid.A ^ Guid.B ^ Guid.C ^ Guid.D;
	}
	Session = S;
}

void FOpenFIREScopeClient::Update()
{
	if (!Socket || LastUpdateFrame == GFrameCounter) return;
	LastUpdateFrame = GFrameCounter;
	const double Now = FPlatformTime::Seconds();

	// 1. answers of the daemon
	uint32 Pending = 0;
	while (Socket->HasPendingData(Pending))
	{
		uint8 Buffer[2048];
		int32 Read = 0;
		if (!Socket->RecvFrom(Buffer, sizeof(Buffer) - 1, Read, *From)) break;
		if (From->GetPort() != DiscoveryPort) continue;   // only the daemon's answers
		Buffer[Read] = 0;
		ParseAnswer(FString(ANSI_TO_TCHAR(reinterpret_cast<const ANSICHAR*>(Buffer))));
		LastAnswer = Now;
		bDaemonSeen = true;
	}
	if (bDaemonSeen && Now - LastAnswer > AnswerTimeout)   // daemon closed
	{
		FMemory::Memzero(Ports);
		bDaemonSeen = false;
	}

	// 2. discovery request once a second: scopes can be paired, switched off or moved at any time
	if (Now >= NextWhoami)
	{
		static const uint8 Whoami[] = { 'W', 'H', 'O', 'A', 'M', 'I' };
		Send(Whoami, sizeof(Whoami), DiscoveryPort);
		NextWhoami = Now + WhoamiEvery;
	}

	// 3. pause packets: the 16-byte header alone, strip 0xFF
	for (int32 P = 1; P <= MaxPlayers; ++P)
	{
		if (!bPausedPlayer[P] || !HasScope(P) || Now < NextPresence[P]) continue;
		uint8 H[HeaderSize];
		WriteHeader(H, Format[P], Frame[P], 0xFF, P);
		Send(H, HeaderSize, PortOf(P));
		NextPresence[P] = Now + PresenceEvery;
	}
}

bool FOpenFIREScopeClient::HasScope(int32 Player) const
{
	return Player >= 1 && Player <= MaxPlayers && (Ports[Player] != 0 || bAlwaysSend);
}

void FOpenFIREScopeClient::SetPaused(int32 Player, bool bPaused)
{
	for (int32 P = 1; P <= MaxPlayers; ++P)
	{
		if (Player != 0 && P != Player) continue;
		if (bPaused && !bPausedPlayer[P]) NextPresence[P] = 0.0;   // first packet at the next Update()
		bPausedPlayer[P] = bPaused;
	}
}

bool FOpenFIREScopeClient::SendFrame(int32 Player, const void* Pixels, EPixels InFormat, int32 Stride, bool bFlipY)
{
	if (!Socket || !Pixels || !HasScope(Player)) return false;
	const int32 Bpp = BytesPerPixel(InFormat);
	const int32 RowBytes = Width * Bpp;
	if (Stride == 0) Stride = RowBytes;
	const uint8* Src = static_cast<const uint8*>(Pixels);

	bPausedPlayer[Player] = false;
	Format[Player] = uint8(InFormat);
	++Frame[Player];   // one number per frame and per player; it may wrap

	for (int32 Strip = 0; Strip < Strips; ++Strip)
	{
		uint8* Out = Packet.GetData();
		WriteHeader(Out, uint8(InFormat), Frame[Player], uint8(Strip), Player);
		Out += HeaderSize;
		for (int32 Row = 0; Row < StripRows; ++Row)
		{
			const int32 Y = Strip * StripRows + Row;
			FMemory::Memcpy(Out, Src + int64(bFlipY ? Height - 1 - Y : Y) * Stride, RowBytes);
			Out += RowBytes;
		}
		Send(Packet.GetData(), HeaderSize + RowBytes * StripRows, PortOf(Player));
	}
	return true;
}

void FOpenFIREScopeClient::WriteHeader(uint8* H, uint8 InFormat, uint32 InFrame, uint8 Strip, int32 Player) const
{
	H[0] = 'O'; H[1] = 'F'; H[2] = 1; H[3] = InFormat;
	H[4] = uint8(InFrame); H[5] = uint8(InFrame >> 8); H[6] = uint8(InFrame >> 16); H[7] = uint8(InFrame >> 24);
	H[8] = Strip; H[9] = Strips; H[10] = uint8(Player);
	H[11] = uint8(Session); H[12] = uint8(Session >> 8); H[13] = uint8(Session >> 16); H[14] = uint8(Session >> 24);
	H[15] = 0;
}

// "OF_SCOPE:P1;PORT=47811;FW=...\nOF_SCOPE:UNPAIRED\n..." -> Ports
void FOpenFIREScopeClient::ParseAnswer(const FString& Text)
{
	int32 Found[MaxPlayers + 1] = {};
	TArray<FString> Lines;
	Text.ParseIntoArrayLines(Lines);
	for (const FString& Line : Lines)
	{
		TArray<FString> Fields;
		Line.TrimStartAndEnd().ParseIntoArray(Fields, TEXT(";"));
		if (Fields.Num() == 0 || Fields[0].Len() != 11 || !Fields[0].StartsWith(TEXT("OF_SCOPE:P"), ESearchCase::CaseSensitive)) continue;
		const int32 Player = Fields[0][10] - TEXT('0');
		if (Player < 1 || Player > MaxPlayers) continue;
		for (const FString& Field : Fields)
		{
			if (!Field.StartsWith(TEXT("PORT="), ESearchCase::CaseSensitive)) continue;
			const FString Value = Field.Mid(5);
			const int32 Port = Value.IsNumeric() ? FCString::Atoi(*Value) : 0;
			if (Port > 0 && Port < 65536) Found[Player] = Port;
		}
	}
	FMemory::Memcpy(Ports, Found, sizeof(Ports));
}

void FOpenFIREScopeClient::Send(const uint8* Data, int32 Length, int32 Port)
{
	// errors are ignored on purpose: a lost strip only makes that frame incomplete
	int32 Sent = 0;
	Target->SetPort(Port);
	Socket->SendTo(Data, Length, Sent, *Target);
}
