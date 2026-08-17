# KyroSpades (Enhanced ButterSpades)
[This project](https://github.com/Kyrope01/KyroSpades) is a modified fork of
[ButterSpades](https://github.com/utf-4096/butterspades), introducing several
quality-of-life improvements and visual enhancements.

Precompiled [releases](https://github.com/Kyrope01/KyroSpades/releases/tag/release)
are available for Android, Windows, MacOS, and Linux (glibc).

KyroSpades has a [Discord server](https://discord.gg/FM7vSxtug4).

## New Features
<dl>
<dt>Skins Menu
<dd>    Now you can easily switch skins of rifle, smg, shotgun, grenade, spade,
intel and tent in the skins menu which apply instantly, even during gameplay.

<dt>Demo Recording and Replay
<dd>    Auto record demo setting added, is turned off by default, but when
enabled starts recording demo automatically. Clicking on a demo in demo list
opens up the demo in the demo replayer.

<dt>Editable Fog Distance and Color in Spectator
<dd>    You can now change fog distance in spectator mode settings and pressing
arrow keys changes color of fog using block's color pallete.

<dt>Gun Sound Variability
<dd>    Each time a shot is fired the sound's pitch and loudness are changed
randomly to give it a slightly different but similar sound every time, which
is a lot better than the very same sound every time.

<dt>Dynamic FOV
<dd>    Fov increases while running and decreases while crouching (has smooth
animations) making gameplay feel much better.

<dt>Chat Log Menu
<dd>    Chat log menu with filters for messages from specific players/the
server and ability to select and copy text.

<dt>Weather
<dd>    Weather settings added where you can turn on rain/snow.

<dt>AO Multiplier
<dd>    Increases ambient occlusion strength (up to 5x) for deeper visual depth.

<dt>Spectator ESP
<dd>    Enables ESP functionality while in spectator mode.

<dt>Custom Mentions
<dd>    Highlight chat messages containing specific words with a customizable color.

<dt>Player Counter
<dd>    A live display of the number of players alive on both teams.

<dt>Dynamic Wallpapers
<dd>    Sets a random image from the <code>png/bg/</code> folder as your
background on startup.

<dt>Always Visible Live Player Counter
<dd>    Adds a setting that forces the player counter to remain visible whenever
GMI is enabled.

<dt>Render Hand
<dd>    Added a new setting called render hand which if enabled, makes your own
hand be visible in FPV.

<dt>Fov Max Limit 130
<dd>    Maximum value for horizontal fov is now increased to 130, its the
highest value till which the game looks good, past that it gets a bit messy.

<dt>Smooth Transition to ADS
<dd>    Zoom in effect added to scope pngs.

<dt>Adjustable ADS FOV
<dd>    ADS FOV (zoom) of each weapon could be individually adjusted in settings
now (this is the only client I know that has this feature :D

<dt>Smooth acceleration of spectator camera
<dd>    Spectator camera now uniformly accelerates up to maximum speed rather
than starting off abruptly at maximum set speed.

<dt>Roll Control for Spectator Mode With Removed Movement Limitations
<dd>    Limitations in movement have been removed and the camera can roll too,
making it pretty similar to how a FPV drone would feel.

<dt>Hit Indicators
<dd>    Hit indicator sounds have been added.

<dt>Chat Scroll
<dd>    Can scroll chat using scroll wheel when chat window is opened.

<dt>Visible Team Scores at Top
<dd>    Score table does not need to be opened for seeing score of teams, a
small score display is always visible.

<dt>Player Statistics Display
<dd>    A setting which when enabled displays player stats on left side of
screen, the stats displayed are: distance traveled, number of jumps, number of
kills, total number of blocks placed, number of headshot kills and the number
of deaths.

<dt>Player Stats and Technical Stats Display
<dd>    Displays player stats (blocks traveled, no. of times jumped, kills,
deaths, no. of headshot kills and blocks placed) and displays technical stats
(no. of voxels loaded, no. of particles loaded, no. of vertices of particles,
no. of particles being created per second).

<dt>Settings Apply Instantly When Changed
<dd>    You now don't need to scroll and click on apply changes button for the
settings to apply, they are applied as soon as a value is changed.

<dt>Custom Coloring
<dd>    You can now adjust exposure, saturation, contrast and vignette of game
during gameplay to make the game look better.

<dt>Improved Settings Menu
<dd>    Settings menu UI has been improved a lot.

<dt>Server Pinning
<dd>    Right clicking on a server in the server list highlights it and pins it
to the top of the server list.

<dt>Textured Blocks
<dd>    Blocks can now have textures when textured blocks are enabled.

<dt>Water Shader and Waves
<dd>    Water blocks are reflecting blocks while fitting to a voxel game.

<dt>Graphical Effects
<dd>    Filmic tonemapping, chromatic aberration, volumetric lighting and lens flare.

<dt>Blood Stains
<dd>    Blood stains option added.

<dt>Damage Numbers
<dd>    Damage done to enemy on hit shown as floating numbers when this option is enabled.

<dt>Discord Rich Presence
<dd>    Shows "Playing KyroSpades" as your Discord activity, with the server name,
live player count and time elapsed (Discord desktop app only; can be turned
off in settings as "Discord Rich Presence").

<dt>Video Recording
<dd>    Record gameplay to MP4 video at a configurable FPS and bitrate by
pressing F7, with system audio capture on Linux (PulseAudio monitor).
Recordings are saved to <code>videos/recordings/</code>.

<dt>Instant Replay Buffer
<dd>    A continuously running replay buffer keeps the last seconds of gameplay
(configurable length); press F8 to instantly save it as an MP4 to
<code>videos/replays/</code>, with on-screen flash feedback.

<dt>Demo Playback Controls
<dd>    While watching a demo you can pause/resume, skip 10 seconds back or
forward and halve/double the playback speed with dedicated hotkeys.

<dt>Persistent Chat Logs
<dd>    All in-game chat is written to daily log files, and the chat log menu
can load older messages from up to 30 days back while connected, so no
conversation is ever lost.

<dt>Chat Input History
<dd>    Press the arrow keys while typing to recall previously sent messages
and commands.

<dt>Chat Customization
<dd>    Chat background opacity, spacing between messages and reversed order
while the chat window is open are all adjustable.

<dt>Free Aim
<dd>    The crosshair moves freely inside an adjustable horizontal/vertical
deadzone before the view starts to follow it.

<dt>Zoomable Map
<dd>    The big map supports 5 zoom levels, cycled with the map zoom key.

<dt>Realistic Sun Shadows
<dd>    Blocks cast directional shadows from the sun with adjustable darkness,
adding real depth to the world.

<dt>Sky Gradient
<dd>    The sky is rendered as a blended gradient with adjustable intensity
instead of a flat color.

<dt>3D Rain and Snow
<dd>    Weather particles can optionally be rendered as full 3D cubes instead
of flat billboards.

<dt>Motion Interpolation and Precise Frame Pacing
<dd>    Rendering interpolates between the fixed 60 Hz physics ticks so motion
stays smooth at any framerate, and the frame cap hits its deadlines exactly.

<dt>Network Smoothing Options
<dd>    Optional smoothing of server position corrections (no visible
rubber-banding), faster position updates, immediate network flushing and
loss-tolerant aim sync for a much better feel on bad connections.

<dt>Raw Aim and Instant Crouch
<dd>    Optional 1:1 aim input with no low-pass filtering so shots exactly
follow the crosshair, and server-instant crouch physics where only the camera
eases down.

<dt>Immersive Camera Feel
<dd>    Subtle view bobbing while walking, a small camera dip when landing from
a fall and camera shake from gunfire and explosions (all view-only; can be
disabled via config.ini).

<dt>New HUD Elements
<dd>    Optional smooth segmented health bar, segmented magazine ammo display
around the crosshair and text shadows on HUD elements.

<dt>Customizable UI Accent With RGB Mode
<dd>    The UI accent color is fully customizable, including an animated RGB
cycling mode with adjustable speed, plus adjustable menu spacing and padding.

<dt>Spectator Player Names
<dd>    Optionally shows player names above characters while spectating.

<dt>Persistent Corpses
<dd>    Option to keep dead player models lying on the map permanently instead
of despawning after a few seconds.
</dl>

## Quick Setup: Dynamic Wallpapers
To add custom wallpapers, drop any `.png` images you like into the `png/bg/`
directory of your client, and the client will cycle through them randomly each
time the game is started.

## Textures:
There is a folder, png/textures; in that folder you can put many square images, when there are images in that folder and textured blocks are enabled textures from the ones you added are used based on average colour.

<br>
Enjoy the enhanced Ace of Spades experience!
<br>
Made with ❤️ by me, for the community :D
