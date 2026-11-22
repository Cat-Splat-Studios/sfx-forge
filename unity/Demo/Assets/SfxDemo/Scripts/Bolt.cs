using UnityEngine;

namespace SfxDemo
{
    /// <summary>A laser bolt: flies, pushes what it hits, is gone after a moment.</summary>
    public class Bolt : MonoBehaviour
    {
        public Vector3 velocity;
        public float life = 0.8f;

        void Update()
        {
            float step = velocity.magnitude * Time.deltaTime;
            if (
                Physics.Raycast(transform.position, velocity.normalized, out RaycastHit hit, step)
                && hit.rigidbody != null
                && hit.rigidbody.GetComponent<HoverCraft>() == null
            )
            {
                hit.rigidbody.AddForceAtPosition(velocity.normalized * 6f, hit.point, ForceMode.Impulse);
                Destroy(gameObject);
                return;
            }

            transform.position += velocity * Time.deltaTime;
            life -= Time.deltaTime;
            if (life <= 0f)
            {
                Destroy(gameObject);
            }
        }
    }
}
