using UnityEngine;

namespace SfxDemo
{
    /// <summary>The demo's camera: it stays behind and above its target, eased, and looks at it.</summary>
    public class FollowCamera : MonoBehaviour
    {
        public Transform target;
        public Vector3 offset = new Vector3(0f, 9f, -12f);

        void LateUpdate()
        {
            if (target == null)
            {
                return;
            }

            Vector3 want = target.position + target.rotation * offset;
            transform.position = Vector3.Lerp(transform.position, want, 1f - Mathf.Exp(-4f * Time.deltaTime));
            transform.LookAt(target.position + Vector3.up);
        }
    }
}
