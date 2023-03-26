//! nwl, for Zig!
//! Top tip! In root do something like...
//! pub const wayland = @import("wayland");
//! Then Wayland types here will become zig-wayland types!

const std = @import("std");

const GlobalIml = extern struct {
    destroy: ?*const fn (?*anyopaque) callconv(.C) void,
};
pub const Global = extern struct {
    link: WlList,
    name: u32,
    global: ?*anyopaque,
    impl: GlobalIml,
};

/// CamelCasify a Wayland object name, to match zig-wayland
const WlObjectNamer = struct {
    fn doWrite(name:[]const u8, writer:anytype) !void {
        var tok = std.mem.tokenize(u8, name, "_");
        while (tok.next()) |t| {
            try writer.writeByte(std.ascii.toUpper(t[0]));
            try writer.writeAll(t[1..]);
        }
    }
    pub fn casify(comptime name:[]const u8) []const u8 {
        var cw = std.io.countingWriter(std.io.null_writer);
        WlObjectNamer.doWrite(name, cw.writer()) catch unreachable;
        var buf:[cw.bytes_written]u8 = undefined;
        var stream = std.io.fixedBufferStream(buf[0..]);
        WlObjectNamer.doWrite(name, stream.writer()) catch unreachable;
        return buf[0..];
    }
};

/// Make a Wayland type, either a opaque or zig-wayland type
fn WaylandObject(comptime name:[]const u8) type {
    const root = @import("root");
    if (@hasDecl(root, "wayland")) {
        const client = root.wayland.client;
        const delim = std.mem.indexOf(u8, name, "_");
        if (delim) |d| {
            const prefix = name[0..d];
            if (@hasDecl(client, prefix)) {
                const sub = @field(client, prefix);
                const camelname = WlObjectNamer.casify(name[d+1..]);
                if (@hasDecl(sub, camelname)) {
                    return @field(sub, camelname);
                }
            }
        }
        else if (@hasDecl(client, name)) {
            return @field(client, name);
        }
    }
    return opaque {};
}

pub const WlDisplay = WaylandObject("wl_display");
pub const WlRegistry = WaylandObject("wl_registry");
pub const WlCompositor = WaylandObject("wl_compositor");
pub const WlShm = WaylandObject("wl_shm");
pub const WlOutput = WaylandObject("wl_output");
pub const ZxdgOutputV1 = WaylandObject("zxdg_output_v1");

pub const XdgWmBase = WaylandObject("xdg_wm_base");
pub const ZxdgOutputManagerV1 = WaylandObject("zxdg_output_manager_v1");
pub const ZwlrLayerShellV1 = WaylandObject("zwlr_layer_shell_v1");
pub const ZxdgDecorationManagerV1 = WaylandObject("zxdg_decoration_manager_v1");
pub const WpViewporter = WaylandObject("wp_viewporter");
pub const WlSubcompositor = WaylandObject("wl_subcompositor");
pub const WlDataDeviceManager = WaylandObject("wl_data_device_manager");
pub const WlBuffer = WaylandObject("wl_buffer");
pub const WlCallback = WaylandObject("wl_callback");
pub const WlDataDevice = WaylandObject("wl_data_device");
pub const WlDataOffer = WaylandObject("wl_data_offer");
pub const WlDataSource = WaylandObject("wl_data_source");
pub const WlKeyboard = WaylandObject("wl_keyboard");
pub const WlPointer = WaylandObject("wl_pointer");
pub const WlProxy = opaque {};
pub const WlRegion = WaylandObject("wl_region");
pub const WlSeat = WaylandObject("wl_seat");
pub const WlShmPool = WaylandObject("wl_shm_pool");
pub const WlSubsurface = WaylandObject("wl_subsurface");
pub const WlSurface = WaylandObject("wl_surface");
pub const WlTouch = WaylandObject("wl_touch");
pub const XdgSurface = WaylandObject("xdg_surface");
pub const ZxdgToplevelDecorationV1 = WaylandObject("zxdg_toplevel_decoration_v1");
pub const WpViewport = WaylandObject("wp_viewport");
pub const XdgPositioner = WaylandObject("xdg_positioner");

pub const XdgToplevel = WaylandObject("xdg_toplevel");
pub const XdgPopup = WaylandObject("xdg_popup");
pub const ZwlrLayerSurfaceV1 = WaylandObject("zwlr_layer_surface_v1");

pub const XkbContext = opaque {};
pub const WlCursorTheme = opaque {};
pub const Poll = opaque {};
const StateEventsImpl = extern struct {
    output_new: ?*const fn (*Output) callconv(.C) void = null,
    output_destroy: ?*const fn (*Output) callconv(.C) void = null,
    global_add: ?*const fn (*State, *const WlRegistry, u32, [*:0]const u8, u32) callconv(.C) bool = null,
    global_remove: ?*const fn (*State, *const WlRegistry, u32) callconv(.C) void = null,
};

pub const Output = extern struct {
    state: *State,
    output: *WlOutput,
    xdg_output: ?*ZxdgOutputV1,
    link: WlList,
    scale: c_int,
    x: i32,
    y: i32,
    width: i32,
    height: i32,
    name: ?[*:0]u8,
};
pub const StateSubImpl = extern struct {
    destroy: ?*const fn (?*anyopaque) callconv(.C) void,
};
pub const StateSub = extern struct {
    link: WlList,
    data: ?*anyopaque,
    impl: *StateSubImpl,
};

// TODO: figure out if it's possible to make this typed.
pub const WlList = extern struct {
    prev: *WlList = undefined,
    next: *WlList = undefined,
};

pub fn WlListHead(comptime linktype:type, comptime field: std.meta.FieldEnum(linktype)) type {
    const tinfo = @typeInfo(linktype);
    if (tinfo != .Struct) {
        @compileError("expected struct, got " ++ .{@typeName(linktype)});
    }
    const fieldinfo = std.meta.fieldInfo(linktype, field);
    if (fieldinfo.type != WlList) {
        @compileError(fieldinfo.name ++ " is a " ++ @typeName(fieldinfo.type) ++ ", not a WlList!");
    }
    return extern struct{
        prev: *WlList = undefined,
        next: *WlList = undefined,
        const Iterator = struct {
            this:*WlList,
            pos:*WlList,

            pub fn next(it:*Iterator) ?*linktype {
                if (it.this != it.pos) {
                    const ret = it.pos;
                    it.pos = it.pos.next;
                    return @fieldParentPtr(linktype, fieldinfo.name, ret);
                }
                return null;
            }
        };
        pub fn iterator(self:*@This()) Iterator {
            return .{
                .this = @ptrCast(*WlList, self),
                .pos = self.next
            };
        }
    };
}

pub const nwl_surface_generic_func_t = *const fn (*Surface) callconv(.C) void;
pub const RendererImpl = extern struct {
    apply_size: nwl_surface_generic_func_t,
    surface_destroy: nwl_surface_generic_func_t,
    swap_buffers: *const fn (*Surface, i32, i32) callconv(.C) void,
    render: nwl_surface_generic_func_t,
    destroy: nwl_surface_generic_func_t,
};
pub const Renderer = extern struct {
    impl: ?*const RendererImpl,
    data: ?*anyopaque,
    rendering: bool,
};
pub const WlArray = extern struct {
    size: usize,
    alloc: usize,
    data: ?*anyopaque,
};

pub const WlFixed = enum(i32) {
    _,
    pub fn toDouble(f: WlFixed) f64 {
        const i = @as(i64, ((1023 + 44) << 52) + (1 << 51)) + @enumToInt(f);
        return @bitCast(f64, i) - (3 << 43);
    }
    pub fn fromDouble(d: f64) WlFixed {
        const i = d + (3 << (51 - 8));
        return @intToEnum(WlFixed, @bitCast(i64, i));
    }
    pub fn toInt(self: WlFixed) c_int {
        return @divTrunc(@enumToInt(self), @as(c_int, 256));
    }
    pub fn fromInt(arg_i: c_int) WlFixed {
        return arg_i * @as(c_int, 256);
    }

    pub fn format(value: WlFixed, comptime fmt: []const u8, options: std.fmt.FormatOptions, writer: anytype) !void {
        try std.fmt.formatType(value.toDouble(), fmt, options, writer, 1);
    }
};

pub const xkb_keysym_t = u32;
pub const xkb_keycode_t = u32;

const SurfaceFlags = packed struct(u32) {
    no_autoscale: bool,
    no_autocursor: bool,
    nwl_frees: bool,
    padding: u29
};

const SurfaceState = packed struct(u32) {
    active: bool,
    maximized: bool,
    fullscreen: bool,
    resizing: bool,
    tiled_left: bool,
    tiled_right: bool,
    tiled_top: bool,
    tiled_bottom: bool,
    csd: bool,
    needs_draw: bool,
    needs_applysize: bool,
    destroy: bool,
    needs_configure: bool,
    _padding:u19
};

const XdgWmCaps = packed struct(u8) {
    window_menu: bool,
    maximize: bool,
    fullscreen: bool,
    minimize: bool,
    _pad:u4
};

const SurfaceRoleUnion = extern union {
    toplevel: extern struct {
        wl: *XdgToplevel,
        decoration: ?*ZxdgToplevelDecorationV1,
        wm_capabilities:XdgWmCaps,
        bounds_width:i32,
        bounds_height:i32
    },
    popup: extern struct {
        wl: *XdgPopup,
        lx:i32,
        ly:i32,
        reposition_token:u32
    },
    layer: extern struct {
        wl: *ZwlrLayerSurfaceV1
    },
    subsurface: extern struct {
        wl: *WlSubsurface
    },
};

pub const nwl_surface_destroy_t = ?*const fn (*Surface) callconv(.C) void;
pub const nwl_surface_input_pointer_t = ?*const fn (*Surface, *Seat, *PointerEvent) callconv(.C) void;
pub const nwl_surface_input_keyboard_t = ?*const fn (*Surface, *Seat, *KeyboardEvent) callconv(.C) void;
pub const nwl_surface_configure_t = ?*const fn (*Surface, u32, u32) callconv(.C) void;
pub const nwl_surface_dnd_t = ?*const fn(*Surface, *Seat, *DndEvent) callconv(.C) void;

const SurfaceImpl = extern struct {
    destroy: nwl_surface_destroy_t,
    input_pointer: nwl_surface_input_pointer_t,
    input_keyboard: nwl_surface_input_keyboard_t,
    dnd: nwl_surface_dnd_t,
    configure: nwl_surface_configure_t,
    close: ?*const fn (*Surface) callconv(.C) void,
};

const SurfaceRoleId = enum(u8) { none, toplevel, popup, layer, sub, cursor, dragicon };

const SurfaceOutputs = extern struct {
    outputs: [*]*Output,
    amount: u32,
};

const DataDevice = extern struct {
    wl: ?*WlDataDevice,
    drop: DataOffer,
    selection: DataOffer,
    incoming: DataOffer,
    event: DndEvent,
};
pub const struct_xkb_keymap = opaque {};
pub const struct_xkb_state = opaque {};
pub const struct_xkb_compose_state = opaque {};
pub const struct_xkb_compose_table = opaque {};
pub const KeyboardEvent = extern struct {
    type: KeyboardEventType,
    compose_state: KeyboardComposeState,
    focus: bool,
    keysym: xkb_keysym_t,
    keycode: xkb_keycode_t,
    utf8: [16]u8,
    serial: u32,
};
pub const WlCursor = opaque {};

const PointerEventChanged = packed struct(u8) { focus: bool, button: bool, motion: bool, axis: bool, _padding: u4 };

const PointerButtons = packed struct(u8) { left: bool, middle: bool, right: bool, back: bool, forward: bool, _padding: u3 };

const PointerAxisSource = packed struct(u8) {
    wheel:bool,
    finger:bool,
    continuous:bool,
    wheel_tilt:bool,
    _pad:u4
};

const PointerAxis = packed struct(u8) {
    vertical:bool,
    horizontal:bool,
    _pad:u6
};

pub const PointerEvent = extern struct {
    changed: PointerEventChanged,
    serial: u32,
    surface_x: WlFixed,
    surface_y: WlFixed,
    buttons: PointerButtons,
    buttons_prev: PointerButtons,
    axis_value_vert: i32,
    axis_value_hori: i32,
    axis_hori: WlFixed,
    axis_vert: WlFixed,
    axis_source: PointerAxisSource,
    axis_stop: PointerAxis,
    focus: bool,
};

const PointerSurface = extern struct {
    xcursor: ?*WlCursor,
    xcursor_surface: ?*WlSurface,
    nwl: ?*Surface,
    hot_x: i32,
    hot_y: i32,
};

pub const Seat = extern struct {
    state: *State,
    link: WlList,
    wl_seat: *WlSeat,
    data_device: DataDevice,
    keyboard: ?*WlKeyboard,
    keyboard_keymap: ?*struct_xkb_keymap,
    keyboard_state: ?*struct_xkb_state,
    keyboard_context: ?*XkbContext,
    keyboard_compose_enabled: bool,
    keyboard_repeat_enabled: bool,
    keyboard_compose_state: ?*struct_xkb_compose_state,
    keyboard_compose_table: ?*struct_xkb_compose_table,
    keyboard_focus: ?*Surface,
    keyboard_repeat_rate: i32,
    keyboard_repeat_delay: i32,
    keyboard_repeat_fd: c_int,
    keyboard_event: ?*KeyboardEvent,
    touch: ?*WlTouch,
    touch_focus: ?*Surface,
    touch_serial: u32,
    pointer: ?*WlPointer,
    pointer_focus: ?*Surface,
    pointer_prev_focus: ?*Surface,
    pointer_surface: PointerSurface,
    pointer_event: ?*PointerEvent,
    name: [*:0]u8,
};

pub const DndEventType = enum(u8) {
    motion = 0,
    enter,
    left,
    drop
};

pub const DndEvent = extern struct {
    type: DndEventType,
    serial: u32,
    focus_surface: ?*Surface,
    x: WlFixed,
    y: WlFixed,
    time: u32,
    source_actions: u32,
    action: u32,
};

pub const DataOffer = extern struct {
    mime: WlArray,
    offer: ?*WlDataOffer,
};

const KeyboardEventType = enum(u8) {
    focus,
    keydown,
    keyup,
    keyrepeat,
    modifiers,
};
const KeyboardComposeState = enum(u8) { none, composing, composed };

pub const nwl_poll_callback_t = *const fn (*State, ?*anyopaque) callconv(.C) void;

pub const cairo_surface_t = opaque {};
pub const nwl_surface_cairo_render_t = *const fn (*Surface, *cairo_surface_t) callconv(.C) void;
extern fn nwl_surface_renderer_cairo(surface: *Surface, renderfunc: nwl_surface_cairo_render_t, flags: i32) void;
pub const surfaceRendererCairo = nwl_surface_renderer_cairo;

extern fn wl_proxy_marshal(p: ?*WlProxy, opcode: u32, ...) void;

const Error = error{ InitFailed, RoleSetFailed, SurfaceCreateFailed };

const SurfaceEvents = struct { destroy: ?*const fn (*const Surface) void = null };

pub const Surface = extern struct {
    link: WlList,
    dirtlink: WlList,
    state: *State,
    wl: extern struct {
        surface: *WlSurface,
        xdg_surface: ?*XdgSurface,
        viewport: ?*WpViewport,
        frame_cb: ?*WlCallback,
    },
    renderer: Renderer,
    width: u32,
    height: u32,
    desired_width: u32,
    desired_height: u32,
    current_width: u32,
    current_height: u32,
    configure_serial: u32,
    scale: c_int,
    parent: ?*Surface,
    subsurfaces: WlList,
    outputs: SurfaceOutputs,
    flags: SurfaceFlags,
    states: SurfaceState,
    title: ?[*:0]u8,
    role_id: SurfaceRoleId,
    role: SurfaceRoleUnion,
    frame: u32,
    impl: SurfaceImpl,
    userdata: ?*anyopaque,
    extern fn nwl_surface_destroy(surface: *Surface) void;
    extern fn nwl_surface_destroy_later(surface: *Surface) void;
    extern fn nwl_surface_set_vp_destination(surface: *Surface, width: i32, height: i32) bool;
    extern fn nwl_surface_set_size(surface: *Surface, width: u32, height: u32) void;
    extern fn nwl_surface_set_title(surface: *Surface, title:?[*:0]const u8) void;
    extern fn nwl_surface_swapbuffers(surface: *Surface, x: i32, y: i32) void;
    extern fn nwl_surface_render(surface: *Surface) void;
    extern fn nwl_surface_set_need_draw(surface: *Surface, rendernow: bool) void;
    extern fn nwl_surface_role_subsurface(surface: *Surface, parent: *Surface) bool;
    extern fn nwl_surface_role_layershell(surface: *Surface, output: ?*WlOutput, layer: u32) bool;
    extern fn nwl_surface_role_toplevel(surface: *Surface) bool;
    extern fn nwl_surface_role_popup(surface: *Surface, parent: *Surface, positioner: *XdgPositioner) bool;
    extern fn nwl_surface_role_unset(surface: *Surface) void;
    extern fn nwl_surface_init(surface: *Surface, state: *State, title:[*:0]const u8) void;

    pub fn commit(self: *Surface) void {
        if (@hasDecl(WlSurface, "commit")) {
            self.wl.surface.commit();
        } else {
            wl_proxy_marshal(@ptrCast(*WlProxy, self.wl.surface), @as(u32, 6));
        }
    }
    pub fn setSize(self: *Surface, width: u32, height: u32) void {
        nwl_surface_set_size(self, width, height);
    }
    pub fn roleTopLevel(self: *Surface) Error!void {
        if (!nwl_surface_role_toplevel(self)) {
            return Error.RoleSetFailed;
        }
    }
    pub fn rolePopup(self: *Surface, parent:*Surface, positioner:*XdgPositioner) Error!void {
        if (!nwl_surface_role_popup(self, parent, positioner)) {
            return Error.RoleSetFailed;
        }
    }
    pub fn roleLayershell(self: *Surface, output: ?*WlOutput, layer: u32) Error!void {
        if (!nwl_surface_role_layershell(self, output, layer)) {
            return Error.RoleSetFailed;
        }
    }
    pub fn roleSubsurface(self: *Surface, parent: *Surface) Error!void {
        if (!nwl_surface_role_subsurface(self, parent)) {
            return Error.RoleSetFailed;
        }
    }
    pub fn render(self: *Surface) void {
        nwl_surface_render(self);
    }
    pub const setTitle = nwl_surface_set_title;
    pub const setVpDestination = nwl_surface_set_vp_destination;
    pub fn swapBuffers(self: *Surface, x: i32, y: i32) void {
        nwl_surface_swapbuffers(self, x, y);
    }
    pub fn setNeedDraw(self: *Surface, rendernow: bool) void {
        nwl_surface_set_need_draw(self, rendernow);
    }
    pub fn destroy(self: *Surface) void {
        nwl_surface_destroy(self);
    }
    pub fn destroyLater(self: *Surface) void {
        nwl_surface_destroy_later(self);
    }
    pub fn unsetRole(self: *Surface) void {
        nwl_surface_role_unset(self);
    }
    pub const init = nwl_surface_init;

    /// Convenience function for casting userdata.
    pub inline fn castData(self: *const Surface, comptime datatype: type) *datatype {
        return @ptrCast(*datatype, @alignCast(@alignOf(datatype), self.userdata));
    }
};

pub const State = extern struct {
    wl: extern struct {
        display: ?*WlDisplay = null,
        registry: ?*WlRegistry = null,
        compositor: ?*WlCompositor = null,
        shm: ?*WlShm = null,
        xdg_wm_base: ?*XdgWmBase = null,
        xdg_output_manager: ?*ZxdgOutputManagerV1 = null,
        layer_shell: ?*ZwlrLayerShellV1 = null,
        decoration: ?*ZxdgDecorationManagerV1 = null,
        viewporter: ?*WpViewporter = null,
        subcompositor: ?*WlSubcompositor = null,
        data_device_manager: ?*WlDataDeviceManager = null,
    } = .{},
    seats: WlListHead(Seat, .link) = .{},
    outputs: WlListHead(Output, .link) = .{},
    surfaces: WlListHead(Surface, .link) = .{},
    surfaces_dirty: WlListHead(Surface, .dirtlink) = .{},
    globals: WlListHead(Global, .link) = .{},
    subs: WlListHead(StateSub, .link) = .{},
    cursor_theme: ?*WlCursorTheme = null,
    cursor_theme_size: u32 = 0,
    num_surfaces: u32 = 0,
    run_with_zero_surfaces: bool = false,
    poll: ?*Poll = null,
    events: StateEventsImpl = .{},
    xdg_app_id: ?[*:0]const u8 = null,
    extern fn nwl_wayland_init(state: *State) u8;
    extern fn nwl_wayland_uninit(state: *State) void;
    extern fn nwl_wayland_run(state: *State) void;
    extern fn nwl_surface_create(state: *State, title: [*:0]const u8) ?*Surface;
    extern fn nwl_state_add_sub(state: *State, subimpl: *StateSubImpl, data: ?*anyopaque) void;
    extern fn nwl_state_get_sub(state: *State, subimpl: *StateSubImpl) ?*anyopaque;
    extern fn nwl_poll_add_fd(state: *State, fd: c_int, callback: nwl_poll_callback_t, data: ?*anyopaque) void;
    extern fn nwl_poll_del_fd(state: *State, fd: c_int) void;
    extern fn nwl_poll_get_fd(state: *State) std.os.fd_t;
    extern fn nwl_poll_dispatch(state: *State, timeout: c_int) bool;

    pub const addSub = nwl_state_add_sub;
    pub const getSub = nwl_state_get_sub;
    pub const addFd = nwl_poll_add_fd;
    pub const delFd = nwl_poll_del_fd;
    pub const getFd = nwl_poll_get_fd;
    pub const dispatch = nwl_poll_dispatch;
    pub fn waylandInit(self: *State) Error!void {
        if (self.nwl_wayland_init() != 0) {
            return Error.InitFailed;
        }
    }
    pub const waylandUninit = nwl_wayland_uninit;
    pub const run = nwl_wayland_run;
    pub fn createSurface(self: *State, title: [*:0]const u8) Error!*Surface {
        return nwl_surface_create(self, title) orelse Error.SurfaceCreateFailed;
    }
};


pub const ShmPool = extern struct {
    fd:std.os.fd_t = -1,
    data:[*]u8 = undefined,
    pool:?*WlShmPool = null,
    size:usize = 0,
};

pub const ShmBuffer = extern struct {
    pub const BufferFlags = packed struct(u8) {
        acquired:bool = false,
        destroy:bool = false,
        _pad:u6 = 0
    };
    wl_buffer:?*WlBuffer = null,
    bufferdata:[*]u8 = undefined,
    data:?*anyopaque = null,
    flags:BufferFlags = .{},

    pub fn release(self:*ShmBuffer) void {
        self.flags.acquired = false;
    }
};

pub const ShmBufferMan = extern struct {
    pub const BufferRendererImpl = extern struct {
        buffer_create:*const fn(buffer:*ShmBuffer, bufferman:*ShmBufferMan) callconv(.C) void,
        buffer_destroy:*const fn(buffer:*ShmBuffer, bufferman:*ShmBufferMan) callconv(.C) void,
    };
    pool:ShmPool = .{},
    buffers:[4]ShmBuffer = [_]ShmBuffer{.{}} ** 4,
    impl:?*const BufferRendererImpl = null,
    width:u32 = 0,
    height:u32 = 0,
    stride:u32 = 0,
    format:u32 = 0,
    num_slots:u8 = 1,

    extern fn nwl_shm_bufferman_get_next(bufferman:*ShmBufferMan) ?*ShmBuffer;
    pub const getNext = nwl_shm_bufferman_get_next;
    extern fn nwl_shm_bufferman_set_slots(bufferman:*ShmBufferMan, state:*State, num_slots:u8) void;
    pub const setSlots = nwl_shm_bufferman_set_slots;
    extern fn nwl_shm_bufferman_resize(bufferman:*ShmBufferMan, state:*State, width:u32, height:u32, stride:u32, format:u32) void;
    pub const resize = nwl_shm_bufferman_resize;
};