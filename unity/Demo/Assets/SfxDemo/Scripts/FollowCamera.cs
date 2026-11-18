using UnityEngine;

namespace SfxDemo
{
    public class FollowCamera : MonoBehaviour
    {
        public Transform target;
        public Vector3 offset = new Vector3(0f, 9f, -12f);

        void LateUpdate()
        {
            if (target == null) return;
            Vector3 want = target.position + target.rotation * offset;
            transform.position = Vector3.Lerp(transform.position, want, 1f - Mathf.Exp(-4f * Time.deltaTime));
            transform.LookAt(target.position + Vector3.up);
        }
    }
}
