## `alienfan-cli`

It's a simple CLI fan control utility for now, providing the same functionality as AWCC (well... a bit more already).
ACPI calls can't control fans directly for modern gear (but can set direct fan percent at older one), so all you can do is set fan boost (Increase RPM above BIOS level).
Run `alienfan-cli [command[=value{,value}] [command...]]`. After start, it detect you gear and show number of fans, temperature sensors and power modes avaliable.
Avaliable commands:
- `usage`, `help` - Show short help
- `rpm[=fanID]` - Show fan RPM(s) for all fans or for fan with this ID only.
- `percent[=fanID]` - Show current fan RPM(s) in percent of maxumum (some systems have non-linear scale) for all fans or for fan with this ID only.
- `temp=[sensorID]` - Show known temperature sensor name and value for all sensors or for selected only.
- `unlock` - Enable manual fan control
- `getpower` - Print current power mode
- `setpower=<value>` - Set system-defined power level. Possible levels autodetected from ACPI, see message at app start 
- `setgpu=<value>` - Set GPU power limit. Possible values from 0 (no limit) to 4 (max. limit).
- `setperf=<ac>,<dc>` - Set CPU Performance boost for AC and battery to desired level. Performance boost can be in 0..4 - disabled, enabled, aggresive, efficient, efficient aggresive.
- `getfans[=mode]` - Show current fan RPMs boost
- `setfans=<fan1>,<fan2>...[,mode]` - Set fans RPM boost level (0..100 - in percent). Fan1 is for CPU fan, Fan2 for GPU one. Number of arguments should be the same as number of fans application detect
- `resetcolor` - Reset color system
- `setcolor=<mask>,r,g,b` - Set light(s) defined by mask to color
- `setcolormode=<brightness>,<flag>` - Set light system brightness and mode. Valid brightness values are 1,3,4,6,7,9,10,12,13,15.
- `direct=<id>,<subid>[,val,val]` - Issue direct Alienware interface command (see below)  
- `directgpu=<id>,<value>` - Issue direct GPU interface command (see below)

`getfans` and `setfans` commands can work into 2 different modes. If mode is zero or absent, boost value will be calculated between 0 and 100% depends of the fan overboost. If mode is non-zero, raw boost value will be dispayed/set. 
FanID and SensorID is a digit from 0 to you current fan/sensor quantity found into the system.

NB: Setting Power level to non-zero value can disabe manual fan control!  

`direct` command is for testing/calling various functions of the main Alienware ACPI function.  
If default functions doesn't works, you can check and try to find you system subset.  

For example: issuing command `direct=20,5,50` return fan RPM for fan #1 on laptop, but for desktop the command should be different.

You can check possible commands and values yourself, opening you system ACPI dump file and searching for `Method(WMAX` function.  
It accept 3 parameters - first is not used, second is a command, and the third is byte array of sumcommand and 2 value bytes.  
Looking inside this method can reveal commands supported for you system and what they do.  
For example, for Aurora R7 command `direct=3,N` return fan ID for fan N or -1 (fail) if fan absent.

You can share commands you find with me, and i'll add it into applications.

`directgpu` command doing the same for GPU subsystem, altering some GPU chip settings. Use with care!

NB: for both `direct` commands, all values are not decimal, but hex (like c8, a3, etc)! It made easy to test values found into ACPI dump.