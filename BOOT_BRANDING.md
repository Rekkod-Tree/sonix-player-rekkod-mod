# Rekkod Mod startup artwork

The startup artwork identifies this firmware as **Rekkod Mod**, with the attribution **на базе Sonix**. Its dark graphite background, restrained blue light and acoustic wave complement Graphite 2. All three boot-theme slots use the same atmospheric identity, avoiding a bright startup flash.

Source: `sonix-player/assets/branding/boot-rekkod.png`. This was generated with the built-in image generation tool, then fitted to each panel and encoded as baseline RGB JPEG. No private fonts are required to regenerate the device assets:

```sh
python3 sonix-player/tools/boot_branding.py
```

Requires Pillow. Outputs are `logo.jpeg`, `logo1.jpeg` and `logo2.jpeg` under both device overlays' `etc/` folders: 480x800 for R1 and 480x720 for R3 Pro II. The script verifies dimensions, colour mode and non-progressive encoding. Boot scripts, raw-flash theme selection and the display executable are unchanged. Actual boot appearance still requires installation on hardware.

## Generation prompt

Create a finished premium boot splash image for a tiny HiBy portable music player running a personal firmware named Rekkod Mod based on Sonix. Portrait 3:5 composition, meant to be downsampled to 480x800 (also center-cropped to 480x720), flat image with no device mockup and no border. Atmospheric but restrained, beautiful and calm, warm listening mood expressed with deep graphite near-black (#11151a), understated icy blue light, very subtle soft cinematic illumination, and a refined abstract sound-wave ribbon or acoustic ripple in the lower third. It should feel like a serious bespoke high-end audio product, not a gaming splash, space wallpaper or busy neon poster. Plenty of dark quiet negative space; gentle texture sufficient to avoid obvious gradient banding on an RGB565 display. Keep small details sparse. Center the typography in a safe middle region: exact main text 'Rekkod' in a clean Segoe UI-like semibold sans serif, large and extremely legible, off-white. Under it much smaller widely spaced 'MOD' in muted icy blue. Under that, exact Cyrillic attribution 'на базе Sonix', smaller but fully legible at 480 pixels wide, soft light grey. No other words or logos. The typography must be crisp and properly spelled. Main word should occupy about 60% of the image width. Art must enhance the quiet atmosphere while remaining secondary to the branding. Keep text and essential art safe inside central 85% height for the shorter display. Output the single final artwork.
