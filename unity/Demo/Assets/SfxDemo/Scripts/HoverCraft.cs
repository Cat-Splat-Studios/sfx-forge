using CatSplat.SfxForge;
using UnityEngine;

namespace SfxDemo
{
    /// <summary>
    /// The demo's vehicle: W/S thrust, A/D turn, SPACE fires. Every sound is an SFX FORGE preset synthesized as it plays;
    /// the engine's pitch and FM follow the speed (SfxTelemetrySender on the engine sound), and the same values go to the
    /// device, so the pad that is held there follows the game.
    /// </summary>
    [RequireComponent(typeof(Rigidbody))]
    public class HoverCraft : MonoBehaviour
    {
        public float thrust = 26f;
        public float turnSpeed = 110f;
        public SfxPlayer engineSound, laserSound, impactSound, pickupSound;
        public Bolt boltPrefab;
        public float arena = 38f;

        Rigidbody body;
        float nextShot;

        public int Score { get; private set; }
        public float Speed => body != null ? body.linearVelocity.magnitude : 0f;

        void Awake() => body = GetComponent<Rigidbody>();

        /// <summary>For the device's encoder 1 (SfxEncoderBinding): more or less engine.</summary>
        public void SetThrust(float value) => thrust = value;

        void Update()
        {
            if (Input.GetKey(KeyCode.Space) && Time.time >= nextShot) Fire();
        }

        void FixedUpdate()
        {
            float forward = Input.GetAxisRaw("Vertical"), turn = Input.GetAxisRaw("Horizontal");
            body.AddForce(transform.forward * (forward * thrust), ForceMode.Acceleration);
            body.MoveRotation(body.rotation * Quaternion.Euler(0f, turn * turnSpeed * Time.fixedDeltaTime, 0f));
            Vector3 p = body.position;                                 // the arena's edge: turn back
            if (Mathf.Abs(p.x) > arena || Mathf.Abs(p.z) > arena)
            {
                body.position = new Vector3(Mathf.Clamp(p.x, -arena, arena), p.y, Mathf.Clamp(p.z, -arena, arena));
                body.linearVelocity = -body.linearVelocity * 0.5f;
            }
        }

        public void Fire()
        {
            nextShot = Time.time + 0.18f;
            if (laserSound != null) laserSound.Play(1f, Random.Range(-2f, 2f));      // never twice the same shot
            if (boltPrefab != null)
            {
                Bolt bolt = Instantiate(boltPrefab, transform.position + transform.forward * 1.6f, transform.rotation);
                bolt.gameObject.SetActive(true);
                bolt.velocity = transform.forward * 60f + body.linearVelocity;
            }
        }

        void OnCollisionEnter(Collision collision)
        {
            float hit = collision.relativeVelocity.magnitude;
            if (hit > 1.5f && impactSound != null) impactSound.Play(Mathf.Clamp01(hit / 18f), Random.Range(-3f, 3f));
        }

        void OnTriggerEnter(Collider other)
        {
            var pickup = other.GetComponent<Pickup>();
            if (pickup == null) return;
            Score++;
            if (pickupSound != null) pickupSound.Play(1f, (Score % 5) * 2f);       // a little higher each time
            pickup.Respawn(arena - 4f);
        }

        void OnGUI()
        {
            var link = SfxLink.Shared;
            GUI.Label(new Rect(12, 10, 700, 22), "W/S thrust   A/D turn   SPACE fire      every sound is synthesized live by SFX FORGE's engine");
            GUI.Label(new Rect(12, 32, 700, 22), $"speed {Speed:0.0}   score {Score}   thrust {thrust:0}");
            GUI.Label(new Rect(12, 54, 700, 22), link.IsOpen ? $"device: in '{link.InputName ?? "-"}'  out '{link.OutputName ?? "-"}'" : "device: not connected (Window > SFX Forge) - the game plays without it");
        }
    }
}
