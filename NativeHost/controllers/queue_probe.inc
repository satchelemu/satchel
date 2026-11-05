    TestController *flood = [TestController new]; flood.haptics = [TestHaptics new];
    dm_haptics_apple_controller(0, (id)flood); drain();
    dispatch_semaphore_t entered = dispatch_semaphore_create(0), release = dispatch_semaphore_create(0);
    dispatch_async(queue, ^{ dispatch_semaphore_signal(entered); dispatch_semaphore_wait(release, DISPATCH_TIME_FOREVER); });
    dispatch_semaphore_wait(entered, DISPATCH_TIME_FOREVER);
    dm_vibration_update burst = {0, 0, {.3f, 160, 0, 320}};
    for (int i = 0; i < 10000; i++) {
        burst.value.amplitude_low = i & 1 ? .3f : .4f;
        dm_haptics_send(&burst, 1);
    }
    burst.value.amplitude_low = .7f; dm_haptics_send(&burst, 1);
    pthread_mutex_lock(&satchel_rumble_lock);
    assert(satchel_rumble_count == SATCHEL_RUMBLE_PENDING && satchel_rumble_overflow == 1 && satchel_rumble_scheduled);
    pthread_mutex_unlock(&satchel_rumble_lock);
    dispatch_semaphore_signal(release);
    drain(); drain(); drain();
    assert(flood.haptics.engines.lastObject.testPlayer.levels.lastObject.floatValue == .7f);
    satchel_haptics_suspend(8, 1); drain();
    assert(dm_haptics_actual(0, 0).amplitude_low == 0);
    send_pair(.9f, 0); drain();
    assert(dm_haptics_actual(0, 0).amplitude_low == 0);
    satchel_haptics_suspend(16, 1); satchel_haptics_suspend(8, 0); send_pair(.8f, 0); drain();
    assert(dm_haptics_actual(0, 0).amplitude_low == 0);
    satchel_haptics_suspend(16, 0); drain();
    assert(dm_haptics_actual(0, 0).amplitude_low == 0);
    send_pair(.6f, 0); drain();
    assert(flood.haptics.engines.lastObject.testPlayer.levels.lastObject.floatValue == .6f);
    puts("PASS: bounded 10001-send flood, final state and nested lifecycle stops");
    // Title rumble strength and cutoff retune a running engine on its own queue, with its clamps.
    satchel_haptics_tune(2.0f, 0.45f); drain();
    assert(gain == 2.0f && rumble_min == 0.45f && controller_level(0.4f) == 0 && controller_level(1) == 1);
    satchel_haptics_tune(9, 5); drain(); assert(gain == 3 && rumble_min == 0.9f);
    satchel_haptics_tune(NAN, -1); drain(); assert(gain == 1 && rumble_min == 0);
    puts("PASS: live rumble strength and cutoff retune with the engine's clamps");
