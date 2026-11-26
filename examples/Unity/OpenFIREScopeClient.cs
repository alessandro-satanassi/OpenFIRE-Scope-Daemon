// OpenFIREScopeClient.cs - the one connection of the game to the OpenFIRE Scope Daemon, for Unity.
//
// It is created by itself the first time an OpenFIREScopeSender needs it (or put it in your first scene to change
// the settings), survives scene changes, calls Update() of the library once per frame and closes the socket when
// the game quits. Needs OpenFIREScope.cs (from the CSharp folder) somewhere in Assets.
//
// MIT License - Copyright (c) 2026 Alessandro Satanassi, Team OpenFIRE

using OpenFIRE;
using UnityEngine;

[DefaultExecutionOrder(-100)]   // before the senders
public class OpenFIREScopeClient : MonoBehaviour
{
    [Tooltip("UDP discovery port of the daemon (47810 unless the user changed it in the daemon settings).")]
    public int discoveryPort = OpenFIREScope.DefaultDiscoveryPort;

    [Tooltip("Testing only: send to the default ports 47811-47814 even when no scope is paired " +
             "(daemon's Preview tab, Tools/fake_daemon.py).")]
    public bool alwaysSend = false;

    [Tooltip("Treat Time.timeScale == 0 as the pause menu: the scopes keep the last image.")]
    public bool pauseWhenTimeScaleIsZero = true;

    /// <summary>Set it while the game shows a pause menu: the scopes keep the last image instead of NO SIGNAL.</summary>
    public static bool Paused { get; set; }

    /// <summary>True while the game is paused (Paused, or timeScale 0 if pauseWhenTimeScaleIsZero).</summary>
    public bool IsPaused => Paused || (pauseWhenTimeScaleIsZero && Time.timeScale == 0f);

    /// <summary>The library object. Null if the socket could not be opened.</summary>
    public OpenFIREScope Scope { get; private set; }

    static OpenFIREScopeClient _instance;
    static bool _quitting;

    /// <summary>The client of the game, created on first use. Null only while the game is quitting.</summary>
    public static OpenFIREScopeClient Instance
    {
        get
        {
            if (_instance == null && !_quitting)
            {
#if UNITY_2023_1_OR_NEWER
                _instance = FindFirstObjectByType<OpenFIREScopeClient>();
#else
                _instance = FindObjectOfType<OpenFIREScopeClient>();
#endif
                if (_instance == null) _instance = new GameObject("OpenFIRE Scope Client").AddComponent<OpenFIREScopeClient>();
            }
            return _instance;
        }
    }

    /// <summary>True if player 1..4 has a paired scope right now.</summary>
    public bool HasScope(int player) => Scope != null && Scope.HasScope(player);

    void Awake()
    {
        if (_instance != null && _instance != this) { Destroy(gameObject); return; }
        _instance = this;
        DontDestroyOnLoad(gameObject);
        try
        {
            Scope = new OpenFIREScope(discoveryPort);
            Scope.AlwaysSend = alwaysSend;
        }
        catch (System.Exception e)
        {
            Debug.LogWarning("OpenFIRE scope: cannot open a UDP socket, the scopes are off. " + e.Message);
        }
    }

    void Update()
    {
        if (Scope == null) return;
        Scope.AlwaysSend = alwaysSend;
        Scope.Update();   // discovery once a second and pause packets; never blocks
    }

    void OnDestroy()
    {
        if (_instance != this) return;
        Scope?.Dispose();   // the scopes show NO SIGNAL about 0.5 s later
        Scope = null;
        _instance = null;
    }

    void OnApplicationQuit()
    {
        _quitting = true;
        OnDestroy();
    }
}
