package tel.schich.libdatachannel;

/**
 * Loads the native for the running platform out of the bundle. Calling this is optional: the
 * library finds the bundled native on its own on first use, this only loads it eagerly.
 */
public abstract class LibDataChannelArchDetect {

    private LibDataChannelArchDetect() {}

    public static void initialize() {
        System.setProperty(Platform.classPathPropertyNameForLibrary(LibDataChannel.LIB_NAME), "/" + Platform.detectArch() + "/native/" + Platform.libraryFilename(LibDataChannel.LIB_NAME));
        LibDataChannel.initialize();
    }
}
