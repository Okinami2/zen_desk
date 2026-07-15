--- fusion_service/src/fusion_service.c
+++ fusion_service/src/fusion_service.c
@@ -93,7 +93,7 @@
 typedef struct {
     int seated;
     int enter_count;
-    int exit_count;
+    uint64_t exit_start_ms;
     LearningState state_before_absent;
     uint64_t last_debug_ms;
 } VisionSeatFilter;
@@ -634,18 +634,22 @@
 
     if (g_vision_seat_filter.seated) {
         if (seen_as_seated_keep) {
-            g_vision_seat_filter.exit_count = 0;
-        } else if (++g_vision_seat_filter.exit_count >= VISION_SEAT_EXIT_SAMPLES) {
-            g_vision_seat_filter.seated = 0;
-            g_vision_seat_filter.enter_count = 0;
-            g_vision_seat_filter.exit_count = 0;
+            g_vision_seat_filter.exit_start_ms = 0;
+        } else {
+            if (g_vision_seat_filter.exit_start_ms == 0) {
+                g_vision_seat_filter.exit_start_ms = now_ms;
+            } else if (now_ms - g_vision_seat_filter.exit_start_ms >= 5000) {
+                g_vision_seat_filter.seated = 0;
+                g_vision_seat_filter.enter_count = 0;
+                g_vision_seat_filter.exit_start_ms = 0;
+            }
         }
     } else {
         if (seen_as_seated_enter) {
             if (++g_vision_seat_filter.enter_count >= VISION_SEAT_ENTER_SAMPLES) {
                 g_vision_seat_filter.seated = 1;
                 g_vision_seat_filter.enter_count = 0;
-                g_vision_seat_filter.exit_count = 0;
+                g_vision_seat_filter.exit_start_ms = 0;
             }
         } else {
             g_vision_seat_filter.enter_count = 0;
@@ -654,10 +658,10 @@
 
     if (g_vision_seat_filter.last_debug_ms == 0 ||
         now_ms - g_vision_seat_filter.last_debug_ms >= VISION_SEAT_DEBUG_INTERVAL_MS) {
-        LOG_INFO("Vision seat sample: face=%u diag=%.1f center=(%.1f,%.1f) center_ok=%d seated=%d enter_count=%d exit_count=%d",
+        LOG_INFO("Vision seat sample: face=%u diag=%.1f center=(%.1f,%.1f) center_ok=%d seated=%d enter_count=%d exit_timer=%llu",
                  vs != NULL ? vs->face_present : 0,
                  diag_sq > 0.0f ? sqrtf(diag_sq) : 0.0f,
                  face_cx, face_cy, center_ok, g_vision_seat_filter.seated,
-                 g_vision_seat_filter.enter_count, g_vision_seat_filter.exit_count);
+                 g_vision_seat_filter.enter_count, g_vision_seat_filter.exit_start_ms == 0 ? 0 : now_ms - g_vision_seat_filter.exit_start_ms);
         g_vision_seat_filter.last_debug_ms = now_ms;
     }
