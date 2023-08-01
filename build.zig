const std = @import("std");

pub const WlScannerStep = struct {
    const Protocol = struct {
        xml: []const u8,
        system: bool,
        file: std.Build.GeneratedFile,
    };
    const WlScannerStepOptions = struct {
        optimize: std.builtin.OptimizeMode,
        target: std.zig.CrossTarget,
        server_headers: bool = false,
        client_header_suffix: []const u8 = "-client-protocol.h",
    };
    const QueueType = std.SinglyLinkedList(Protocol);
    step: std.Build.Step,
    queue: QueueType,
    lib: *std.build.CompileStep,
    dest_path: []const u8,
    gen_server_headers: bool,
    client_header_suffix: []const u8,
    const Self = @This();
    pub fn create(b: *std.Build, options: WlScannerStepOptions) !*Self {
        const res = try b.allocator.create(Self);
        const dest_path = try b.cache_root.join(b.allocator, &.{"wl-gen"});
        res.* = .{
            .step = std.Build.Step.init(.{
                .id = .custom,
                .name = "wayland-scanner",
                .owner = b,
                .makeFn = make,
            }),
            .queue = .{},
            .lib = b.addStaticLibrary(.{
                .name = "wayland-protocol-lib",
                .target = options.target,
                .optimize = options.optimize,
            }),
            .dest_path = dest_path,
            .gen_server_headers = options.server_headers,
            .client_header_suffix = options.client_header_suffix,
        };
        // Smarten this up, perhaps..
        const incpath = std.mem.trim(u8, b.exec(&.{ "pkg-config", "--variable=includedir", "wayland-client" }), &std.ascii.whitespace);
        res.lib.addIncludePath(.{ .path = incpath });
        res.lib.linkLibC();
        return res;
    }
    pub fn linkWith(self: *Self, lib: *std.Build.CompileStep) void {
        lib.linkLibrary(self.lib);
        lib.addIncludePath(.{ .path = self.dest_path });
    }
    pub fn addProtocol(self: *Self, xml: []const u8, system: bool) void {
        const node = self.step.owner.allocator.create(QueueType.Node) catch @panic("OOM");
        node.data = .{
            .xml = xml,
            .file = .{ .step = &self.step },
            .system = system,
        };
        self.lib.addCSourceFile(.{ .file = .{ .generated = &node.data.file }, .flags = &.{} });
        self.queue.prepend(node);
    }
    pub fn addProtocolFromPath(self: *Self, base: []const u8, xml: []const u8) void {
        self.addProtocol(self.step.owner.pathJoin(&.{ base, xml }), false);
    }
    pub fn addSystemProtocols(self: *Self, xmls: []const []const u8) void {
        for (xmls) |x| {
            self.addProtocol(x, true);
        }
    }
    pub fn addProtocolsFromPath(self: *Self, base: []const u8, xmls: []const []const u8) void {
        for (xmls) |x| {
            self.addProtocolFromPath(base, x);
        }
    }

    const WlScanGenType = enum {
        code,
        clientheader,
        serverheader,
        pub fn toString(self: WlScanGenType) []const u8 {
            switch (self) {
                .code => return "private-code",
                .clientheader => return "client-header",
                .serverheader => return "server-header",
            }
        }
    };

    const WlScanError = error{WaylandScannerFail};

    fn runScanner(self: *Self, protocol: []const u8, gentype: WlScanGenType, output: []const u8) !void {
        var code: u8 = undefined;
        _ = try self.step.owner.execAllowFail(&.{ "wayland-scanner", gentype.toString(), protocol, output }, &code, .Ignore);
    }

    fn processProtocol(self: *Self, ally: std.mem.Allocator, protocol: *QueueType.Node.Data, gentype: WlScanGenType, destdir: *const std.fs.Dir, realdestpath: []const u8) !void {
        const protoname = std.fs.path.stem(protocol.xml);
        const filesuffix = switch (gentype) {
            .code => ".c",
            .clientheader => self.client_header_suffix,
            .serverheader => "-protocol.h",
        };
        const filename = try std.mem.concat(ally, u8, &.{ protoname, filesuffix });
        var needscanner = brk: {
            const outstat = destdir.statFile(filename) catch break :brk true;
            const instat = std.fs.cwd().statFile(protocol.xml) catch unreachable;
            break :brk outstat.mtime < instat.mtime;
        };
        if (needscanner) {
            var fullpath = try std.fs.path.join(ally, &.{ realdestpath, filename });
            try self.runScanner(protocol.xml, gentype, fullpath);
            // Should use the cache system here instead of... this..
            self.step.result_cached = false;
        }
        if (gentype == .code) {
            const path = self.step.owner.pathJoin(&.{ self.dest_path, filename });
            protocol.file.path = path;
        }
    }

    pub fn process(self: *Self, protocol: *QueueType.Node.Data, dest: *const std.fs.Dir, realdestpath: []const u8) !void {
        var buf: [2048]u8 = undefined;
        var fba = std.heap.FixedBufferAllocator.init(buf[0..]);
        var ally = fba.allocator();
        try self.processProtocol(ally, protocol, .code, dest, realdestpath);
        fba.reset();
        try self.processProtocol(ally, protocol, .clientheader, dest, realdestpath);
        if (self.gen_server_headers) {
            fba.reset();
            try self.processProtocol(ally, protocol, .serverheader, dest, realdestpath);
        }
    }

    pub fn make(step: *std.Build.Step, progress: *std.Progress.Node) !void {
        _ = progress;
        const self = @fieldParentPtr(Self, "step", step);
        const builder = step.owner;
        var it = self.queue.first;
        if (self.queue.first == null) {
            return;
        }
        var sysprotopath: ?[]const u8 = null;
        step.result_cached = true;
        const dest = try std.fs.cwd().makeOpenPath(builder.pathFromRoot(self.dest_path), .{});
        const realdestpath = try dest.realpathAlloc(builder.allocator, ".");
        while (it) |node| : (it = node.next) {
            if (node.data.system) {
                if (sysprotopath == null) {
                    sysprotopath = try self.getSysWlProtocolsDir();
                }
                var buf: [2048]u8 = undefined;
                var fba = std.heap.FixedBufferAllocator.init(&buf);
                const newpath = try std.fs.path.join(fba.allocator(), &.{ sysprotopath.?, node.data.xml });
                node.data.xml = newpath;
            }
            try self.process(&node.data, &dest, realdestpath);
        }
    }
    fn getSysWlProtocolsDir(self: *Self) ![]const u8 {
        // Don't really like doing this, but you gotta do what you gotta do..
        var code: u8 = undefined;
        const systemprotodir = try self.step.owner.execAllowFail(&.{ "pkg-config", "--variable=pkgdatadir", "wayland-protocols" }, &code, .Ignore);
        return std.mem.trim(u8, systemprotodir, &std.ascii.whitespace);
    }
};

pub fn build(b: *std.build.Builder) !void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});
    const nwl = b.addStaticLibrary(.{ .name = "nwl", .target = target, .optimize = optimize });
    _ = b.addModule("nwl", .{ .source_file = .{ .path = "zig/nwl.zig" } });
    // Maybe have an OptionsStep as well so features are reflected in nwl.zig?
    const egl = b.option(bool, "egl", "EGL support") orelse false;
    const cairo = b.option(bool, "cairo", "Cairo renderer") orelse false;
    const seat = b.option(bool, "seat", "Wayland seat support") orelse false;
    const scannerstep = try WlScannerStep.create(b, .{
        .target = target,
        .optimize = optimize,
        .client_header_suffix = ".h",
    });
    scannerstep.linkWith(nwl);
    scannerstep.addSystemProtocols(&.{
        "stable/xdg-shell/xdg-shell.xml",
        "unstable/xdg-decoration/xdg-decoration-unstable-v1.xml",
        "unstable/xdg-output/xdg-output-unstable-v1.xml",
    });
    scannerstep.addProtocol(b.pathFromRoot("protocol/wlr-layer-shell-unstable-v1.xml"), false);
    nwl.linkLibC();
    nwl.linkSystemLibrary("wayland-client");
    nwl.addIncludePath(.{.path = "."});
    nwl.addCSourceFiles(&.{
        "src/shell.c",
        "src/shm.c",
        "src/surface.c",
        "src/wayland.c",
    }, &.{});
    if (seat) {
        nwl.linkSystemLibrary("xkbcommon");
        nwl.linkSystemLibrary("wayland-cursor");
        nwl.addCSourceFile(.{.file = .{.path = "src/seat.c"}, .flags = &.{}});
        scannerstep.addSystemProtocols(&.{
            "staging/cursor-shape/cursor-shape-v1.xml",
            "unstable/tablet/tablet-unstable-v2.xml",
        });
    }
    if (egl) {
        nwl.addCSourceFile(.{.file = .{.path = "src/egl.c"}, .flags = &.{}});
        nwl.linkSystemLibrary("wayland-egl");
        nwl.linkSystemLibrary("epoxy");
    }
    if (cairo) {
        nwl.addCSourceFile(.{.file = .{.path = "src/cairo.c"}, .flags = &.{}});
        nwl.linkSystemLibrary("cairo");
    }
    const conf = b.addConfigHeader(.{ .include_path = "nwl/config.h" }, .{
        .NWL_HAS_SEAT = @intFromBool(seat),
    });
    nwl.addConfigHeader(conf);
    b.installArtifact(nwl);
    nwl.installHeadersDirectoryOptions(.{
        .source_dir = .{ .path = "nwl" },
        .install_dir = .header,
        .install_subdir = "nwl",
        .exclude_extensions = &.{"build"},
    });
}
