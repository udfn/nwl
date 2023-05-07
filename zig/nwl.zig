//! nwl, for Zig!
//! Top tip! In root do something like...
//! pub const wayland = @import("wayland");
//! Then Wayland types here will become zig-wayland types!

const std = @import("std");

pub const Global = extern struct {
    const Impl = extern struct {
        destroy: ?*const fn (?*anyopaque) callconv(.C) void,
    };
    link: WlList,
    name: u32,
    global: ?*anyopaque,
    impl: Impl,
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
pub const XdgPositioner = WaylandObject("xdg_positioner");

pub const XdgToplevel = WaylandObject("xdg_toplevel");
pub const XdgPopup = WaylandObject("xdg_popup");
pub const ZwlrLayerSurfaceV1 = WaylandObject("zwlr_layer_surface_v1");

pub const XkbContext = opaque {};
pub const WlCursorTheme = opaque {};
pub const Poll = opaque {};

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
    destroy: ?*const fn (*StateSub) callconv(.C) void,
};
pub const StateSub = extern struct {
    link: WlList,
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

fn WlArray(comptime T:type) type {
    return extern struct {
        size: usize,
        alloc: usize,
        data: ?[*]T,

        pub fn slice(self:@This()) []T {
            return self.data.?[0..self.size/@sizeOf(T)];
        }
    };
}

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

const DataDevice = extern struct {
    wl: ?*WlDataDevice,
    drop: DataOffer,
    selection: DataOffer,
    incoming: DataOffer,
    event: DndEvent,
};
pub const KeyboardEvent = extern struct {
    const EventType = enum(u8) {
        focus,
        keydown,
        keyup,
        keyrepeat,
        modifiers,
    };
    const XkbKeysym = u32;
    const XkbKeycode = u32;
    const ComposeState = enum(u8) { none, composing, composed };
    type: EventType,
    compose_state: ComposeState,
    focus: bool,
    keysym: XkbKeysym,
    keycode: XkbKeycode,
    utf8: [16]u8,
    serial: u32,
};

pub const PointerEvent = extern struct {
    const Changed = packed struct(u8) { focus: bool, button: bool, motion: bool, axis: bool, _padding: u4 };
    const Buttons = packed struct(u8) { left: bool, middle: bool, right: bool, back: bool, forward: bool, _padding: u3 };
    const AxisSource = packed struct(u8) {
        wheel:bool,
        finger:bool,
        continuous:bool,
        wheel_tilt:bool,
        _pad:u4
    };
    const Axis = packed struct(u8) {
        vertical:bool,
        horizontal:bool,
        _pad:u6
    };
    changed: Changed,
    serial: u32,
    surface_x: WlFixed,
    surface_y: WlFixed,
    buttons: Buttons,
    buttons_prev: Buttons,
    axis_value_vert: i32,
    axis_value_hori: i32,
    axis_hori: WlFixed,
    axis_vert: WlFixed,
    axis_source: AxisSource,
    axis_stop: Axis,
    focus: bool,
};


const PointerSurface = extern struct {
    const WlCursor = opaque {};
    xcursor: ?*WlCursor,
    xcursor_surface: ?*WlSurface,
    nwl: ?*Surface,
    hot_x: i32,
    hot_y: i32,
};

pub const Seat = extern struct {
    const XkbKeymap = opaque {};
    const XkbState = opaque {};
    const XkbComposeState = opaque {};
    const XkbComposeTable = opaque {};

    state: *State,
    link: WlList,
    wl_seat: *WlSeat,
    data_device: DataDevice,
    keyboard: ?*WlKeyboard,
    keyboard_keymap: ?*XkbKeymap,
    keyboard_state: ?*XkbState,
    keyboard_context: ?*XkbContext,
    keyboard_compose_enabled: bool,
    keyboard_repeat_enabled: bool,
    keyboard_compose_state: ?*XkbComposeState,
    keyboard_compose_table: ?*XkbComposeTable,
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

pub const DndEvent = extern struct {
    const EventType = enum(u8) {
        motion = 0,
        enter,
        left,
        drop
    };
    type: EventType,
    serial: u32,
    focus_surface: ?*Surface,
    x: WlFixed,
    y: WlFixed,
    time: u32,
    source_actions: u32,
    action: u32,
};

pub const DataOffer = extern struct {
    mime: WlArray([*:0]u8),
    offer: ?*WlDataOffer,
};
pub const CairoSurface = extern struct {
    const cairo_surface_t = opaque {};
    const cairo_t = opaque {};
    ctx:*cairo_t,
    surface:*cairo_surface_t,
    rerender:bool,
};
const nwl_surface_cairo_render_t = *const fn (*Surface, *CairoSurface) callconv(.C) void;
const CairoRendererFlags = packed struct(c_int) {
    damage_tracking:bool = false,
    _pad:i31 = 0
};
extern fn nwl_surface_renderer_cairo(surface: *Surface, renderfunc: nwl_surface_cairo_render_t, flags: CairoRendererFlags) void;
pub const surfaceRendererCairo = nwl_surface_renderer_cairo;

extern fn wl_proxy_marshal(p: ?*WlProxy, opcode: u32, ...) void;

const Error = error{ InitFailed, RoleSetFailed, SurfaceCreateFailed };

pub const Surface = extern struct {
    const GenericSurfaceFn = *const fn (*Surface) callconv(.C) void;
    const RoleId = enum(u8) { none, toplevel, popup, layer, sub, cursor, dragicon };
    const Flags = packed struct(u32) {
        no_autoscale: bool = false,
        no_autocursor: bool = false,
        nwl_frees: bool = false,
        padding: u29 = 0
    };

    const SurfaceStates = packed struct(u32) {
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

    pub const Renderer = extern struct {
        pub const Impl = extern struct {
            // Bug or feature? Using GenericSurfaceFn here is a dependency loop error..
            apply_size: *const fn (*Surface) callconv(.C) void,
            swap_buffers: *const fn (*Surface, i32, i32) callconv(.C) void,
            render: *const fn (*Surface) callconv(.C) void,
            destroy: *const fn (*Surface) callconv(.C) void,
        };
        impl: ?*const Impl = null,
        data: ?*anyopaque = undefined,
        rendering: bool = false,
    };
    const SurfaceImpl = extern struct {
        destroy: ?GenericSurfaceFn = null,
        input_pointer: ?*const fn (*Surface, *Seat, *PointerEvent) callconv(.C) void = null,
        input_keyboard: ?*const fn (*Surface, *Seat, *KeyboardEvent) callconv(.C) void = null,
        dnd: ?*const fn(*Surface, *Seat, *DndEvent) callconv(.C) void = null,
        configure: ?*const fn (*Surface, u32, u32) callconv(.C) void = null,
        close: ?GenericSurfaceFn = null,
    };
    const RoleUnion = extern union {
        toplevel: extern struct {
            const Capabilities = packed struct(u8) {
                window_menu: bool,
                maximize: bool,
                fullscreen: bool,
                minimize: bool,
                _pad:u4
            };
            wl: *XdgToplevel,
            decoration: ?*ZxdgToplevelDecorationV1,
            wm_capabilities:Capabilities,
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
            wl: *WlSubsurface,
            parent: *Surface,
        },
    };

    link: WlList = .{},
    dirtlink: WlList = .{},
    state: *State = undefined,
    wl: extern struct {
        surface: *WlSurface,
        xdg_surface: ?*XdgSurface,
        frame_cb: ?*WlCallback,
    } = undefined,
    renderer: Renderer = .{},
    width: u32 = undefined,
    height: u32 = undefined,
    desired_width: u32 = 640,
    desired_height: u32 = 480,
    current_width: u32 = undefined,
    current_height: u32 = undefined,
    configure_serial: u32 = undefined,
    scale: i32 = undefined,
    scale_preferred: i32 = undefined,
    subsurfaces: WlList = .{},
    outputs: extern struct {
        outputs: [*]*Output,
        amount: u32,
    } = undefined,
    flags: Flags = .{},
    states: SurfaceStates = undefined,
    title: ?[*:0]u8 = null,
    role_id: RoleId = undefined,
    role: RoleUnion = undefined,
    frame: u32 = 0,
    impl: SurfaceImpl = .{},
    userdata: ?*anyopaque = undefined,
    extern fn nwl_surface_destroy(surface: *Surface) void;
    extern fn nwl_surface_destroy_later(surface: *Surface) void;
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
    pub const setSize = nwl_surface_set_size;
    pub const render = nwl_surface_render;
    pub const setTitle = nwl_surface_set_title;
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
    pub const PollCallbackFn = *const fn (*State, ?*anyopaque) callconv(.C) void;
    wl: extern struct {
        display: ?*WlDisplay = null,
        registry: ?*WlRegistry = null,
        compositor: ?*WlCompositor = null,
        shm: ?*WlShm = null,
        xdg_wm_base: ?*XdgWmBase = null,
        xdg_output_manager: ?*ZxdgOutputManagerV1 = null,
        layer_shell: ?*ZwlrLayerShellV1 = null,
        decoration: ?*ZxdgDecorationManagerV1 = null,
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
    events: extern struct {
        output_new: ?*const fn (*Output) callconv(.C) void = null,
        output_destroy: ?*const fn (*Output) callconv(.C) void = null,
        global_add: ?*const fn (*State, *WlRegistry, u32, [*:0]const u8, u32) callconv(.C) bool = null,
        global_remove: ?*const fn (*State, *WlRegistry, u32) callconv(.C) void = null,
    } = .{},
    xdg_app_id: ?[*:0]const u8 = null,
    extern fn nwl_wayland_init(state: *State) u8;
    extern fn nwl_wayland_uninit(state: *State) void;
    extern fn nwl_wayland_run(state: *State) void;
    extern fn nwl_surface_create(state: *State, title: [*:0]const u8) ?*Surface;
    extern fn nwl_state_add_sub(state: *State, sub: *StateSub) void;
    extern fn nwl_state_get_sub(state: *State, impl: *StateSubImpl) ?*StateSub;
    extern fn nwl_poll_add_fd(state: *State, fd: c_int, callback: PollCallbackFn, data: ?*anyopaque) void;
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

    extern fn nwl_shm_pool_finish(pool:*ShmPool) void;
    pub const finish = nwl_shm_pool_finish;
};

pub const ShmBufferMan = extern struct {
    pub const max_buffers = 4;
    pub const Buffer = extern struct {
        const Flags = packed struct(u8) {
            acquired:bool = false,
            destroy:bool = false,
            _pad:u6 = 0
        };
        wl_buffer:?*WlBuffer = null,
        bufferdata:[*]u8 = undefined,
        flags:Flags = .{},

        pub fn release(self:*Buffer) void {
            self.flags.acquired = false;
        }
    };
    pub const RendererImpl = extern struct {
        buffer_create:*const fn(buf_idx:c_uint, bufferman:*ShmBufferMan) callconv(.C) void,
        buffer_destroy:*const fn(buf_idx:c_uint, bufferman:*ShmBufferMan) callconv(.C) void,
    };
    pool:ShmPool = .{},
    buffers:[max_buffers]Buffer = [_]Buffer{.{}} ** max_buffers,
    impl:?*const RendererImpl = null,
    width:u32 = 0,
    height:u32 = 0,
    stride:u32 = 0,
    format:u32 = 0,
    num_slots:u8 = 1,

    extern fn nwl_shm_bufferman_get_next(bufferman:*ShmBufferMan) c_int;
    pub fn getNext(bufferman:*ShmBufferMan) !c_uint {
        const ret = bufferman.nwl_shm_bufferman_get_next();
        if (ret != -1) {
            return @intCast(c_uint, ret);
        }
        return error.NoAvailableBuffer;
    }
    extern fn nwl_shm_bufferman_set_slots(bufferman:*ShmBufferMan, state:*State, num_slots:u8) void;
    pub const setSlots = nwl_shm_bufferman_set_slots;
    extern fn nwl_shm_bufferman_resize(bufferman:*ShmBufferMan, state:*State, width:u32, height:u32, stride:u32, format:u32) void;
    pub const resize = nwl_shm_bufferman_resize;
    extern fn nwl_shm_bufferman_finish(bufferman:*ShmBufferMan) void;
    pub const finish = nwl_shm_bufferman_finish;
};