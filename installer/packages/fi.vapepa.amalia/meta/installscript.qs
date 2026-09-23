function Component() {}

Component.prototype.createOperations = function() {
    component.createOperations();
    if (systemInfo.productType === "windows") {
        component.addOperation("CreateShortcut",
            "@TargetDir@/bin/amalia-launcher.exe",
            "@StartMenuDir@/Amalia.lnk",
            "workingDirectory=@TargetDir@/bin");
        component.addOperation("CreateShortcut",
            "@TargetDir@/bin/amalia-launcher.exe",
            "@DesktopDir@/Amalia.lnk",
            "workingDirectory=@TargetDir@/bin");
    }
}
