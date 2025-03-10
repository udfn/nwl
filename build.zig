const std = @import("std");
const WlScannerStep = @import("wlscannerstep").WlScannerStep;

pub fn build(b: *std.Build) !void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});
    // Maybe have an OptionsStep as well so features are reflected in nwl.zig?
    const egl = b.option(bool, "egl", "EGL support") orelse false;
    const cairo = b.option(bool, "cairo", "Cairo renderer") orelse false;
    const seat = b.option(bool, "seat", "Wayland seat support") orelse false;
    const dynamic = b.option(bool, "dynamic", "Dynamically link nwl, useful for working around lld logspam") orelse false;
    const nwl_lib_mod = b.createModule(.{
        .target = target,
        .optimize = optimize,
        .link_libc = true,
    });
    const nwl_lib = b.addLibrary(.{
        .name = "nwl",
        .linkage = if (dynamic) .dynamic else .static,
        .root_module = nwl_lib_mod,
    });
    const mod = b.addModule("nwl", .{
        .root_source_file = b.path("zig/nwl.zig"),
    });
    // This is to shut lld up, specifically complaints about archive members being neither ET_REL nor LLVM bitcode
    if (dynamic) {
        mod.linkLibrary(nwl_lib);
    } else {
        mod.addObject(b.addObject(.{
            .name = "nwl",
            .root_module = nwl_lib_mod,
        }));
    }

    const scannerstep = try WlScannerStep.create(b, .{
        .target = target,
        .optimize = optimize,
        .client_header_suffix = ".h",
    });
    scannerstep.lib.root_module.pic = dynamic;
    scannerstep.linkWith(nwl_lib_mod);
    scannerstep.addSystemProtocols(&.{
        "stable/xdg-shell/xdg-shell.xml",
        "unstable/xdg-decoration/xdg-decoration-unstable-v1.xml",
        "unstable/xdg-output/xdg-output-unstable-v1.xml",
    });
    scannerstep.addProtocol(b.path("protocol/wlr-layer-shell-unstable-v1.xml"));
    nwl_lib_mod.addIncludePath(b.path("."));
    nwl_lib_mod.linkSystemLibrary("wayland-client", .{});
    nwl_lib_mod.addCSourceFiles(.{ .files = &.{
        "src/shell.c",
        "src/shm.c",
        "src/surface.c",
        "src/wayland.c",
    } });
    if (seat) {
        nwl_lib_mod.linkSystemLibrary("xkbcommon", .{});
        nwl_lib_mod.linkSystemLibrary("wayland-cursor", .{});
        nwl_lib_mod.addCSourceFile(.{ .file = b.path("src/seat.c"), .flags = &.{} });
        scannerstep.addSystemProtocols(&.{
            "staging/cursor-shape/cursor-shape-v1.xml",
            "unstable/tablet/tablet-unstable-v2.xml",
        });
    }
    if (egl) {
        nwl_lib_mod.addCSourceFile(.{ .file = b.path("src/egl.c"), .flags = &.{} });
        nwl_lib_mod.linkSystemLibrary("wayland-egl", .{});
        nwl_lib_mod.linkSystemLibrary("epoxy", .{});
    }
    if (cairo) {
        nwl_lib.addCSourceFile(.{ .file = b.path("src/cairo.c"), .flags = &.{} });
        nwl_lib.linkSystemLibrary("cairo");
    }
    const conf = b.addConfigHeader(.{ .include_path = "nwl/config.h" }, .{
        .NWL_HAS_SEAT = @intFromBool(seat),
    });
    nwl_lib_mod.addConfigHeader(conf);
    b.installArtifact(nwl_lib);
    nwl_lib.installHeadersDirectory(b.path("nwl"), "nwl", .{
        .exclude_extensions = &.{"build"},
    });
}
