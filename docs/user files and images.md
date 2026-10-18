# User files and image textures

## `app.user_io`

Files live in `%AppData%\AxEng\app_user\<project name>`. Paths outside that folder are rejected and log an error.

- `app.user_io.open(name, mode)` returns a handle with `read_all()`, `write(text)` (returns whether it succeeded) and `close()`, or nil if the file can't be opened.
- `app.user_io.remove(name)` deletes a file and returns true, or false if it doesn't exist or couldn't be removed.

## Textures from encoded images

`app.res.create_temp_texture_from_image(name, png_data[, max_width, max_height])` decodes PNG bytes (for example read with `app.user_io`) into a temporary texture, like `app.res.create_temp_texture`. If the image is larger than the optional limits it is box-filtered down by the smallest whole factor that fits, which gives smooth previews and keeps memory low. Release it with `app.res.release_temp_texture` when done. Unlike `app.res.create_texture`, which keeps the texture for the lifetime of the app, this is meant for images that come and go, such as save screenshots.

An existing temporary texture with the same name is returned as is, so use a new name when the image changes.

## Frame captures

`app.window.capture_next_frame_user(name)` (and `capture_next_frame_io(path)` with the IO permission) saves the next rendered frame as a PNG and fires `app.window.on_frame_image` when done. The window is opaque, so the saved alpha channel is always 255 even where only the clear colour was drawn. Requests made during an update callback capture the frame rendered right after it.
