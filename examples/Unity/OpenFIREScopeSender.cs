// OpenFIREScopeSender.cs - renders a scope camera and sends it to the OpenFIRE scope of one player.
//
// Put it on a Camera (the scope camera, not the main camera). It gives the camera a 240x240 render texture, keeps
// the camera off while that player has no scope, reads the image back from the GPU without stalling
// (AsyncGPUReadback) and sends it, at most maxFps times per second. Works with the built-in pipeline, URP and HDRP.
// Add OpenFIREScopeAim to the same camera to point it where the gun aims.
//
// MIT License - Copyright (c) 2026 Alessandro Satanassi, Team OpenFIRE

using System.Collections;
using OpenFIRE;
using UnityEngine;
using UnityEngine.Rendering;

[RequireComponent(typeof(Camera))]
public class OpenFIREScopeSender : MonoBehaviour
{
    [Tooltip("The player whose scope shows this camera.")]
    [Range(1, 4)] public int player = 1;

    [Tooltip("Images per second at most. The daemon shows 30 to 60 per second; more is wasted.")]
    [Range(10, 60)] public int maxFps = 60;

    [Tooltip("Unity reads textures back with the bottom row first. Untick it only if the scope shows the image " +
             "upside down (check it in the daemon's Preview tab).")]
    public bool flipY = true;

    Camera _camera;
    RenderTexture _target;
    Texture2D _cpuCopy;                                    // only without AsyncGPUReadback support
    readonly byte[] _pixels = new byte[OpenFIREScope.Width * OpenFIREScope.Height * 4];
    bool _waiting;                                         // a readback is in flight
    float _nextSend;

    void OnEnable()
    {
        _camera = GetComponent<Camera>();
        _target = new RenderTexture(OpenFIREScope.Width, OpenFIREScope.Height, 24, RenderTextureFormat.ARGB32)
        {
            name = "OpenFIRE scope P" + player,
        };
        _target.Create();
        _camera.targetTexture = _target;   // the scope camera never draws on the screen
        _camera.enabled = false;
        StartCoroutine(SendAtEndOfFrames());
    }

    void OnDisable()
    {
        StopAllCoroutines();
        if (_camera != null) { _camera.targetTexture = null; _camera.enabled = false; }
        if (_target != null) { _target.Release(); Destroy(_target); _target = null; }
        if (_cpuCopy != null) { Destroy(_cpuCopy); _cpuCopy = null; }
        _waiting = false;
    }

    void LateUpdate()
    {
        var client = OpenFIREScopeClient.Instance;
        if (client == null) return;   // quitting
        // the camera renders only while that player has a scope and the game is not paused: no scope, no cost
        _camera.enabled = client.HasScope(player) && !client.IsPaused;
        if (client.HasScope(player) && client.IsPaused) client.Scope.SetPaused(player, true);   // keeps the last image
    }

    IEnumerator SendAtEndOfFrames()
    {
        var endOfFrame = new WaitForEndOfFrame();
        while (true)
        {
            yield return endOfFrame;   // the scope camera has rendered this frame
            if (!_camera.enabled || _waiting || Time.unscaledTime < _nextSend) continue;
            _nextSend = Mathf.Max(_nextSend + 1f / maxFps, Time.unscaledTime - 1f / maxFps);

            if (SystemInfo.supportsAsyncGPUReadback)
            {
                _waiting = true;
                AsyncGPUReadback.Request(_target, 0, TextureFormat.RGBA32, OnReadback);
            }
            else
            {
                // old devices: a synchronous read, the GPU waits (240x240 is small)
                if (_cpuCopy == null) _cpuCopy = new Texture2D(OpenFIREScope.Width, OpenFIREScope.Height, TextureFormat.RGBA32, false);
                var previous = RenderTexture.active;
                RenderTexture.active = _target;
                _cpuCopy.ReadPixels(new Rect(0, 0, OpenFIREScope.Width, OpenFIREScope.Height), 0, 0, false);
                RenderTexture.active = previous;
                _cpuCopy.GetRawTextureData<byte>().CopyTo(_pixels);
                Send();
            }
        }
    }

    void OnReadback(AsyncGPUReadbackRequest request)
    {
        _waiting = false;
        if (request.hasError || _target == null) return;
        request.GetData<byte>().CopyTo(_pixels);   // no allocation: our own buffer
        Send();
    }

    void Send()
    {
        var client = OpenFIREScopeClient.Instance;
        if (client == null || client.Scope == null || client.IsPaused) return;
        client.Scope.SendFrame(player, _pixels, ScopePixels.RGBA8888, 0, flipY);
    }
}
