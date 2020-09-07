import 'dart:async';

import 'package:battery_plus_platform_interface/battery_plus_platform_interface.dart';
import 'package:meta/meta.dart';

import 'upower_device.dart';

// ### TODO: introduce a new 'unknown' battery state for workstations?
extension _ToBatteryState on UPowerBatteryState {
  BatteryState toBatteryState() {
    switch (this) {
      case UPowerBatteryState.charging:
        return BatteryState.charging;
      case UPowerBatteryState.discharging:
        return BatteryState.discharging;
      default:
        return BatteryState.full;
    }
  }
}

@visibleForTesting
typedef UPowerDeviceFactory = UPowerDevice Function();

/// The Linux implementation of BatteryPlatform.
class BatteryLinux extends BatteryPlatform {
  /// Returns the current battery level in percent.
  @override
  Future<int> get batteryLevel {
    final device = createDevice();
    return device
        .getPercentage()
        .then((value) => value.round())
        .whenComplete(() => device.dispose());
  }

  /// Fires whenever the battery state changes.
  @override
  Stream<BatteryState> get onBatteryStateChanged {
    _stateController ??= StreamController<BatteryState>(
      onListen: _startListenState,
      onCancel: _stopListenState,
    );
    return _stateController.stream.asBroadcastStream();
  }

  UPowerDevice _stateDevice;
  StreamController<BatteryState> _stateController;

  @visibleForTesting
  UPowerDeviceFactory createDevice = () => UPowerDevice.display();

  void _addState(UPowerBatteryState value) {
    _stateController.add(value.toBatteryState());
  }

  void _startListenState() {
    _stateDevice ??= createDevice();
    // ### TODO: is an initial state needed?
    _stateDevice.getState().then((value) => _addState(value));
    _stateDevice.subscribeStateChanged().listen((value) => _addState(value));
  }

  void _stopListenState() {
    _stateDevice?.dispose();
    _stateDevice = null;
  }
}
