def test():
    vision_seated = 0 # Face is small
    radar_seated = 1 # Radar still detects motion
    seated = vision_seated or radar_seated
    print(f"seated: {seated}")
test()
