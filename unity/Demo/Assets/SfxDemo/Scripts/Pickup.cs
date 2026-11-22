using UnityEngine;

namespace SfxDemo
{
    /// <summary>Something to collect: spins, and comes back somewhere else.</summary>
    public class Pickup : MonoBehaviour
    {
        void Update() => transform.Rotate(0f, 120f * Time.deltaTime, 0f, Space.World);

        public void Respawn(float range) =>
            transform.position = new Vector3(
                Random.Range(-range, range),
                transform.position.y,
                Random.Range(-range, range)
            );
    }
}
