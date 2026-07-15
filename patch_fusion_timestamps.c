--- fusion_service/src/fusion_service.c
+++ fusion_service/src/fusion_service.c
@@ -724,7 +724,7 @@
     float face_cx = 0.0f;
     float face_cy = 0.0f;
 
-    now_ms = (vs->timestamp != 0) ? vs->timestamp : fusion_monotonic_ms();
+    now_ms = fusion_monotonic_ms();
 
     if (vs->face_present == 0 ||
         vs->attention_region == VISION_ATTENTION_NO_FACE ||
@@ -853,7 +853,7 @@
     int vision_seated = vision_seat_update(vs, &face_diag_sq, &face_cx, &face_cy, &center_ok);
     int radar_seated = 0;
     
-    now_ms = (vs->timestamp != 0) ? vs->timestamp : fusion_monotonic_ms();
+    now_ms = fusion_monotonic_ms();
     if (g_fusion_service.latest_radar.timestamp != 0 &&
         now_ms >= g_fusion_service.latest_radar.timestamp &&
         now_ms - g_fusion_service.latest_radar.timestamp < 5000) {
@@ -895,7 +895,7 @@
             restore_state = STATE_FOCUSED;
         }
         g_fusion_service.current_state = restore_state;
-        g_fusion_service.last_tick_ms = (vs->timestamp != 0) ? vs->timestamp : fusion_monotonic_ms();
+        g_fusion_service.last_tick_ms = fusion_monotonic_ms();
         vision_focus_reset(restore_state == STATE_DISTRACTED);
         should_dispatch = 1;
         LOG_INFO("Vision seat -> PRESENT (face=%u diag=%.1f center=(%.1f,%.1f) center_ok=%d restore=%d)",
@@ -919,7 +919,7 @@
         g_vision_filter.is_distracted = 1;
     }
 
-    now_ms = (vs->timestamp != 0) ? vs->timestamp : fusion_monotonic_ms();
+    now_ms = fusion_monotonic_ms();
     vote = vision_state_to_vote(vs);
     if (vision_focus_apply_vote(vote, now_ms, &next_state)) {
         if (g_fusion_service.current_state != next_state) {
@@ -1096,6 +1096,7 @@
 void fusion_update_radar(const RadarState *state) {
     pthread_mutex_lock(&g_fusion_service.mutex);
     g_fusion_service.latest_radar = *state;
+    g_fusion_service.latest_radar.timestamp = fusion_monotonic_ms();
     pthread_mutex_unlock(&g_fusion_service.mutex);
 }
 
