/* libtinymail-gnome-desktop-1.0.vapi generated by vapigen, do not modify. */

[CCode (cprefix = "Tny", lower_case_cprefix = "tny_")]
namespace Tny {
	[CCode (cheader_filename = "tny-gnome-platform-factory.h")]
	public class GnomeAccountStore : GLib.Object, Tny.AccountStore {
		public weak Tny.SessionCamel get_session ();
		public GnomeAccountStore ();
	}
	[CCode (cheader_filename = "tny-gnome-platform-factory.h")]
	public class GnomeDevice : GLib.Object, Tny.Device {
		public GnomeDevice ();
	}
	[CCode (cheader_filename = "tny-gnome-platform-factory.h")]
	public class GnomePlatformFactory : GLib.Object, Tny.PlatformFactory {
		public static weak Tny.PlatformFactory get_instance ();
	}
}
