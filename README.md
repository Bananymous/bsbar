# BSBAR

Status generator for i3bar. I didn't like i3status, so I decided to build by own :D

### Building

#### Dependencies

You will only need to install *premake5* and *pulseaudio* development packages. Other dependencies are [*tomlplusplus*](https://github.com/marzer/tomlplusplus) and [*nlohmann::json*](https://github.com/nlohmann/json) which are included in this repo as header only libraries.

#### Building

	git clone https://github.com/bananymous/bsbar
	cd bsbar
	premake5 gmake2
	make config=release

The binary can be found in `bin/Release/bsbar`

### Configuration

Config file can be stored in `$HOME/.config/bsbar/config.toml` or it can be specified as an argument to bsbar.

Config file formatted using [toml](https://toml.io/en/).

Config in [config/config.toml](/config/config.toml) now now server as reference until I get to make full default configuration.

#### Global configurations

| Key		| Accepts			| Default			| Description										|
|-----------|-------------------|-------------------|---------------------------------------------------|
| `order`	| list of strings	| *required*		| Defines the order of blocks from left to right.	|


#### Configurations that apply for every block

| Key			| Accepts			| Default			| Description																		|
|---------------|-------------------|-------------------|-----------------------------------------------------------------------------------|
| `type`		| string			| *required*	| Defines the type of a block.														|
| `format`		| string			| *required*	| Format of the block. All occurences of `%value%` is replaced by block's value.	|
| `interval`	| integer			| 1					| Number of seconds between block updates.											|
| `signal`		| list of integers	| none				| Block gets refereshed if bsbar recieves any of the specified signals. Signals are defined as offset to `SIGRTMIN`. e.g. 3 corresponds to `SIGRTMIN+3`. |
| `needed`		| boolean			| false				| Should main thread wait for block's update to finish before displaying blocks. Recommended for `datetime` blocks. |
| `ramp`		| list of strings	| none				| List of strings to replace `%ramp%` in 'format' depending on value of the block. E.g. If 2 strings are given, first will be used when value is 0-50 and latter when value is 50-100. |
| `value-min`	| number			| 0					| Minimum value for block. Used as lower bound in `ramp`.							|
| `value-max`	| number			| 100				| Maximum value for block. Used as upper bound in `ramp`.							|
| `precision`	| integer			| 0					| Number of decimals shown after blocks value in `%value%` substitution.			|
| `color`		| string			| none (white)		| Set the text color on this block. See format in i3bar protocol, linked below.		|
|+ every key described in [i3bar protocol](https://i3wm.org/docs/i3bar-protocol.html).||||



#### Click events

Click command can be defined in config in two different ways

	[block]
	type = "..."
	format = "..."
	click.command = "..."

or

	[block]
	type = "..."
	format = "..."

	[block.click]
	command = "..."

| Key				| Accepts	| Default	| Description																									|
|-------------------|-----------|-----------|---------------------------------------------------------------------------------------------------------------|
| `command`			| string	| none		| Command to execute when this block is clicked.																|
| `blocking` 		| boolean	| false		| Should the click-handling-thread block until the `command` is finished. (no idea when this would be needed)	|
| `single-instance` | boolean	| false		| Is there only allowed to be single instance of the `command` running.											|



#### Configuration of 'Battery' block

	type = "internal/battery"

Block's value is set to battery percentage (0 - 100).

| Key				| Accepts			| Default	| Description																						|
|-------------------|-------------------|-----------|---------------------------------------------------------------------------------------------------|
| `battery`			| string			| *"BAT0"*	| Which battery to use. Battery is searched from <code>/sys/class/power_supply/*battery*/</code>	|
| `ramp-charging`	| list of strings	| none		| Alternative `ramp` used when the battery is charging.												|



#### Configuration of 'Custom' block

	type = "custom"

| Key				| Accepts	| Default	| Description												|
|-------------------|-----------|-----------|-----------------------------------------------------------|
| `text-command`	| string	| none		| Command whose output is replaces ```%text%``` in *format*.|
| `value-command`	| string	| none		| Command whose output will be assigned to block's value.	|



#### Configuration of 'DateTime' block

	type = "internal/datetime"

Does not have any custom keys. `format` uses <code>[std::strftime](https://en.cppreference.com/w/c/chrono/strftime)</code> formatting.



#### Configuration of 'Menu' block

	type = "internal/menu"

Menu is a toggleable block that can show/hide other blocks defined as its sub-blocks.

Sub-blocks are defined in the config as <code>[*menu-block-name*.sub*n*]</code> where *n* is a integer. Blocks in menu are ordered with respect *n*. Lowest *n* being on left and highest on right.

| Key				| Accepts	| Default	| Description													|
|-------------------|-----------|-----------|---------------------------------------------------------------|
| `show-default`	| boolean	| false		| Are blocks in the menu are shown on startup.					|
| `timeout`			| integer	| 0			| Number of seconds the menu stays open. 0 disables timeout.	|





#### Configuration of 'Network' block

	type = "internal/network"

`%ipv4%` or `%ipv6%` can be used in `format` which get replaced by interface's IPv4 and IPv6 respectively

| Key					| Accepts	| Default			| Description																	|
|-----------------------|-----------|-------------------|-------------------------------------------------------------------------------|
| `interface`			| string	| *required*	| Network interface to use														|
| `format-disconnected`	| string	| none				| Alternative `format` that is used when the interface is not connected.		|
| `color-auto`			| boolean	| false				| Is block's `color` automatically mapped from rssi (-100 - 0 => red - green).	|




#### Configuration of 'PulseAudio' block

	type = "internal/pulseaudio.output"
	type = "internal/pulseaudio.input"

Currenly upports only default sink/source. 

Block's value is set to current volume.

| Key					| Accepts	| Default	| Description																					|
|-----------------------|-----------|-----------|-----------------------------------------------------------------------------------------------|
| `format-muted`		| string	| none		| Alternative `format` for when the sink/source is muted.										|
| `color-muted`			| string	| none		| Alternative `color` shown when block sink/source is muted.									|
| `click-to-mute`		| boolean	| true		| Does clicking on this block toggles sink/source mute											|
| `max-volume`			| number	| 100		| *(output only)* Limits the maximum volume of sink this block can set.							|
| `volume-step`			| number	| 5			| *(output only)* Amount to change volume per scroll event.										|
| `global-volume-cap`	| boolean	| true		| *(output only)* Should bsbar also limit volume set by other programs.							|
| `enable-scroll`		| boolean	| true		| *(output only)* Does scrolling the mouse wheel while hovering this block modify sink volume.	|





#### Configuration of 'Temperature' block

	type = "internal/temperature"

Block's value is set to current temperature (in celcius).

| Key				| Accepts	| Default			| Description																										|
|-------------------|-----------|-------------------|-------------------------------------------------------------------------------------------------------------------|
| `thermal-zone`	| string	| *"thermal_zone0"*	| Thermal zone to use in this block. Thremal zone is searched from <code>/sys/class/thermal/*thermal-zone*/</code>	|
