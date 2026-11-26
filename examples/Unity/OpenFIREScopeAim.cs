// OpenFIREScopeAim.cs - points a scope camera where the gun aims, with a zoom.
//
// Put it on the scope camera (with OpenFIREScopeSender). Each frame it places the camera at the main camera and
// turns it along the ray that goes through the aim point on the screen, and gives it a field of view "zoom" times
// narrower. The image stays upright: the scope itself rotates it with the roll of the gun.
//
// The aim point is the mouse by default (an OpenFIRE lightgun moves the mouse). With several lightguns, set
// useMouse = false and write AimScreenPosition every frame from your own input code.
//
// MIT License - Copyright (c) 2026 Alessandro Satanassi, Team OpenFIRE

using UnityEngine;

[RequireComponent(typeof(Camera))]
public class OpenFIREScopeAim : MonoBehaviour
{
    [Tooltip("The camera the player sees on the screen. Empty = Camera.main.")]
    public Camera mainCamera;

    [Tooltip("Magnification of the scope: 4 means a field of view 4 times narrower than the main camera.")]
    [Range(1f, 20f)] public float zoom = 4f;

    [Tooltip("Aim with the mouse. Untick it to set AimScreenPosition from your own code.")]
    public bool useMouse = true;

    /// <summary>The aim point in screen pixels (0,0 = bottom left), as Input.mousePosition.</summary>
    public Vector2 AimScreenPosition { get; set; }

    Camera _scope;

    void Awake() => _scope = GetComponent<Camera>();

    // after the game has moved the main camera, before rendering
    void LateUpdate()
    {
        var main = mainCamera != null ? mainCamera : Camera.main;
        if (main == null || main == _scope) return;
        if (useMouse) AimScreenPosition = MousePosition();

        Ray ray = main.ScreenPointToRay(AimScreenPosition);
        transform.SetPositionAndRotation(ray.origin, Quaternion.LookRotation(ray.direction, main.transform.up));

        // the same magnification for any main field of view: tan(fov/2) divided by the zoom
        float half = Mathf.Tan(main.fieldOfView * 0.5f * Mathf.Deg2Rad) / Mathf.Max(1f, zoom);
        _scope.fieldOfView = 2f * Mathf.Atan(half) * Mathf.Rad2Deg;
        _scope.aspect = 1f;   // the scope is square
        _scope.nearClipPlane = main.nearClipPlane;
        _scope.farClipPlane = main.farClipPlane;
    }

    static Vector2 MousePosition()
    {
#if ENABLE_INPUT_SYSTEM && !ENABLE_LEGACY_INPUT_MANAGER
        var mouse = UnityEngine.InputSystem.Mouse.current;
        return mouse != null ? mouse.position.ReadValue() : Vector2.zero;
#else
        return Input.mousePosition;
#endif
    }
}
