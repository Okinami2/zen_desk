--- vision_service/src/vision_display.c
+++ vision_service/src/vision_display.c
@@ -316,6 +316,9 @@
 
     // if (show_test_pattern(&g_display) != 0) {
     //     vision_display_stop();
     //     return TD_FAILURE;
     // }
+    if (ioctl(g_display.fb_fd, FBIO_REFRESH, &g_display.canvas_buf) < 0) {
+        perror("vision display: FBIO_REFRESH failed");
+    }
 
