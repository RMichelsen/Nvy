# Nvy
Nvy is a minimal [Neovim](https://neovim.io/) client for Windows written in C++.\
It uses DirectWrite to shape and render the grid cells and text.\
Fonts can be changed by setting the guifont
in `init.vim`, for example:
`set guifont=Fira\ Code:h24`

![](resources/client.png)

# Configuration
Nvy sets the global vim variable `g:nvy = 1` in case you want to specialize your init.vim while using Nvy.

Nvy can be started with the following flags:
- `--maximize` to start in fullscreen
- `--geometry=<cols>x<rows>` to start with a given number of rows and columns, e.g. `--geometry=80x25`

# Extra Features
- You can use Alt+Enter to toggle fullscreen
- You can use Ctrl+Mousewheel to zoom

# Releases
Releases can be found [here](https://github.com/RMichelsen/Nvy/releases)

# Build & Dependencies
Building should be straight forward, there are no external dependencies.\
The only dependency Nvy uses is the excellent [MPack](https://github.com/ludocode/mpack) library
which is compiled alongside the client itself.
