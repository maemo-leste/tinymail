/* libtinymail-1.0.vapi generated by vapigen, do not modify. */

[CCode (cprefix = "Tny", lower_case_cprefix = "tny_")]
namespace Tny {
	[CCode (cprefix = "TNY_ACCOUNT_", cheader_filename = "tny.h")]
	public enum AccountSignal {
		CONNECTION_STATUS_CHANGED,
		CHANGED,
		LAST_SIGNAL
	}
	[CCode (cprefix = "TNY_ACCOUNT_TYPE_", cheader_filename = "tny.h")]
	public enum AccountType {
		STORE,
		TRANSPORT
	}
	[CCode (cprefix = "TNY_ALERT_TYPE_", cheader_filename = "tny.h")]
	public enum AlertType {
		INFO,
		WARNING,
		ERROR
	}
	[CCode (cprefix = "TNY_CONNECTION_STATUS_", cheader_filename = "tny.h")]
	public enum ConnectionStatus {
		DISCONNECTED,
		DISCONNECTED_BROKEN,
		CONNECTED_BROKEN,
		CONNECTED,
		RECONNECTING,
		INIT
	}
	[CCode (cprefix = "TNY_ERROR_", cheader_filename = "tny.h")]
	public enum Error {
	}
	[CCode (cprefix = "TNY_ERROR_DOMAIN_", cheader_filename = "tny.h")]
	public enum ErrorDomain {
	}
	[CCode (cprefix = "TNY_FOLDER_", cheader_filename = "tny.h")]
	public enum FolderSignal {
		FOLDER_INSERTED,
		FOLDERS_RELOADED,
		LAST_SIGNAL
	}
	[CCode (cprefix = "TNY_FOLDER_TYPE_", cheader_filename = "tny.h")]
	public enum FolderType {
		UNKNOWN,
		NORMAL,
		INBOX,
		OUTBOX,
		TRASH,
		JUNK,
		SENT,
		ROOT,
		NOTES,
		DRAFTS,
		CONTACTS,
		CALENDAR,
		ARCHIVE,
		MERGE
	}
	[CCode (cprefix = "TNY_ACCOUNT_STORE_", cheader_filename = "tny.h")]
	public enum GetAccountsRequestType {
		TRANSPORT_ACCOUNTS,
		STORE_ACCOUNTS,
		BOTH
	}
	[CCode (cprefix = "TNY_SEND_QUEUE_CANCEL_ACTION_", has_type_id = "0", cheader_filename = "tny.h")]
	public enum SendQueueCancelAction {
		SUSPEND,
		REMOVE
	}
	[CCode (cprefix = "TNY_", cheader_filename = "tny.h")]
	public enum StatusCode {
		FOLDER_STATUS_CODE_REFRESH,
		FOLDER_STATUS_CODE_GET_MSG,
		GET_MSG_QUEUE_STATUS_GET_MSG,
		FOLDER_STATUS_CODE_XFER_MSGS,
		FOLDER_STATUS_CODE_COPY_FOLDER,
		GET_SUPPORTED_SECURE_AUTH_STATUS_GET_SECURE_AUTH,
		FOLDER_STATUS_CODE_SYNC
	}
	[CCode (cprefix = "TNY_", cheader_filename = "tny.h")]
	public enum StatusDomain {
		FOLDER_STATUS,
		GET_MSG_QUEUE_STATUS,
		GET_SUPPORTED_SECURE_AUTH_STATUS
	}
	[CCode (cprefix = "TNY_FOLDER_CAPS_", cheader_filename = "tny.h")]
	[Flags]
	public enum FolderCaps {
		WRITABLE,
		PUSHEMAIL
	}
	[CCode (cprefix = "TNY_FOLDER_CHANGE_CHANGED_", cheader_filename = "tny.h")]
	[Flags]
	public enum FolderChangeChanged {
		ALL_COUNT,
		UNREAD_COUNT,
		ADDED_HEADERS,
		EXPUNGED_HEADERS,
		FOLDER_RENAME,
		MSG_RECEIVED
	}
	[CCode (cprefix = "TNY_FOLDER_STORE_CHANGE_CHANGED_", cheader_filename = "tny.h")]
	[Flags]
	public enum FolderStoreChangeChanged {
		CREATED_FOLDERS,
		REMOVED_FOLDERS
	}
	[CCode (cprefix = "TNY_FOLDER_STORE_QUERY_OPTION_", cheader_filename = "tny.h")]
	[Flags]
	public enum FolderStoreQueryOption {
		SUBSCRIBED,
		UNSUBSCRIBED,
		MATCH_ON_NAME,
		MATCH_ON_ID,
		PATTERN_IS_CASE_INSENSITIVE,
		PATTERN_IS_REGEX
	}
	[CCode (cprefix = "TNY_HEADER_FLAG_", cheader_filename = "tny.h")]
	[Flags]
	public enum HeaderFlags {
		ANSWERED,
		DELETED,
		DRAFT,
		FLAGGED,
		SEEN,
		ATTACHMENTS,
		CACHED,
		PARTIAL,
		EXPUNGED,
		HIGH_PRIORITY,
		NORMAL_PRIORITY,
		LOW_PRIORITY,
		SUSPENDED
	}
	[CCode (cheader_filename = "tny.h")]
	public class TError {
	}
	[CCode (copy_function = "tny_status_copy", cheader_filename = "tny.h")]
	public class Status {
		public GLib.Quark domain;
		public int code;
		public weak string message;
		public uint position;
		public uint of_total;
		public weak Tny.Status copy ();
		public double get_fraction ();
		public bool matches (GLib.Quark domain, int code);
		public Status (GLib.Quark domain, int code, uint position, uint of_total, string format);
		public Status.literal (GLib.Quark domain, int code, uint position, uint of_total, string message);
		public void set_fraction (double fraction);
	}
	[CCode (cheader_filename = "tny.h")]
	public class CombinedAccount : GLib.Object, Tny.FolderStore, Tny.Account, Tny.TransportAccount, Tny.StoreAccount {
		public weak Tny.StoreAccount get_store_account ();
		public weak Tny.TransportAccount get_transport_account ();
		public CombinedAccount (Tny.TransportAccount ta, Tny.StoreAccount sa);
	}
	[CCode (cheader_filename = "tny.h")]
	public class FolderChange : GLib.Object {
		public void add_added_header (Tny.Header header);
		public void add_expunged_header (Tny.Header header);
		public void get_added_headers (Tny.List headers);
		public Tny.FolderChangeChanged get_changed ();
		public bool get_check_duplicates ();
		public void get_expunged_headers (Tny.List headers);
		public weak Tny.Folder get_folder ();
		public uint get_new_all_count ();
		public uint get_new_unread_count ();
		public weak Tny.Msg get_received_msg ();
		public weak string get_rename (string oldname);
		public FolderChange (Tny.Folder folder);
		public void reset ();
		public void set_check_duplicates (bool check_duplicates);
		public void set_new_all_count (uint new_all_count);
		public void set_new_unread_count (uint new_unread_count);
		public void set_received_msg (Tny.Msg msg);
		public void set_rename (string newname);
	}
	[CCode (cheader_filename = "tny.h")]
	public class FolderMonitor : GLib.Object, Tny.FolderObserver {
		public FolderMonitor (Tny.Folder folder);
		public virtual void add_list (Tny.List list);
		public virtual void poke_status ();
		public virtual void remove_list (Tny.List list);
		public virtual void start ();
		public virtual void stop ();
		[NoWrapper]
		public virtual void update (Tny.FolderChange change);
	}
	[CCode (cheader_filename = "tny.h")]
	public class FolderStats : GLib.Object {
		public uint get_all_count ();
		public weak Tny.Folder get_folder ();
		public ulong get_local_size ();
		public uint get_unread_count ();
		public FolderStats (Tny.Folder folder);
		public void set_local_size (ulong local_size);
	}
	[CCode (cheader_filename = "tny.h")]
	public class FolderStoreChange : GLib.Object {
		public void add_created_folder (Tny.Folder folder);
		public void add_removed_folder (Tny.Folder folder);
		public Tny.FolderStoreChangeChanged get_changed ();
		public void get_created_folders (Tny.List folders);
		public weak Tny.FolderStore get_folder_store ();
		public void get_removed_folders (Tny.List folders);
		public FolderStoreChange (Tny.FolderStore folderstore);
		public void reset ();
	}
	[CCode (cheader_filename = "tny.h")]
	public class FolderStoreQuery : GLib.Object {
		public weak Tny.List items;
		public void add_item (string pattern, Tny.FolderStoreQueryOption options);
		public weak Tny.List get_items ();
		public FolderStoreQuery ();
	}
	[CCode (cheader_filename = "tny.h")]
	public class FolderStoreQueryItem : GLib.Object {
		public Tny.FolderStoreQueryOption options;
		public void* regex;
		public weak string pattern;
		public Tny.FolderStoreQueryOption get_options ();
		public weak string get_pattern ();
		public void* get_regex ();
	}
	[CCode (cheader_filename = "tny.h")]
	public class FsStream : GLib.Object, Tny.Stream, Tny.Seekable {
		public FsStream (int fd);
		public void set_fd (int fd);
	}
	[CCode (cheader_filename = "tny.h")]
	public class MergeFolder : GLib.Object, Tny.Folder, Tny.FolderObserver {
		public void add_folder (Tny.Folder folder);
		public void get_folders (Tny.List list);
		public MergeFolder (string folder_name);
		public MergeFolder.with_ui_locker (string folder_name, Tny.Lockable ui_locker);
		public void remove_folder (Tny.Folder folder);
		public void set_folder_type (Tny.FolderType folder_type);
		public void set_ui_locker (Tny.Lockable ui_locker);
	}
	[CCode (cheader_filename = "tny.h")]
	public class NoopLockable : GLib.Object, Tny.Lockable {
		public NoopLockable ();
	}
	[CCode (cheader_filename = "tny.h")]
	public class Pair : GLib.Object {
		public weak string get_name ();
		public weak string get_value ();
		public Pair (string name, string value);
		public void set_name (string name);
		public void set_value (string value);
	}
	[CCode (cheader_filename = "tny.h")]
	public class SimpleList : GLib.Object, Tny.List {
		public SimpleList ();
	}
	[CCode (cheader_filename = "tny.h")]
	public interface Account : GLib.Object {
		public abstract void cancel ();
		public abstract Tny.AccountType get_account_type ();
		public abstract weak Tny.ConnectionPolicy get_connection_policy ();
		public abstract Tny.ConnectionStatus get_connection_status ();
		public abstract Tny.ForgetPassFunc get_forget_pass_func ();
		public abstract weak string get_hostname ();
		public abstract weak string get_id ();
		public abstract weak string get_name ();
		public abstract Tny.GetPassFunc get_pass_func ();
		public abstract uint get_port ();
		public abstract weak string get_proto ();
		public abstract weak string get_secure_auth_mech ();
		public abstract weak string get_url_string ();
		public abstract weak string get_user ();
		public abstract bool is_ready ();
		public abstract bool matches_url_string (string url_string);
		public abstract void set_connection_policy (Tny.ConnectionPolicy policy);
		public abstract void set_forget_pass_func (Tny.ForgetPassFunc forget_pass_func);
		public abstract void set_hostname (string host);
		public abstract void set_id (string id);
		public abstract void set_name (string name);
		public abstract void set_pass_func (Tny.GetPassFunc get_pass_func);
		public abstract void set_port (uint port);
		public abstract void set_proto (string proto);
		public abstract void set_secure_auth_mech (string mech);
		public abstract void set_url_string (string url_string);
		public abstract void set_user (string user);
		public abstract void start_operation (Tny.StatusDomain domain, Tny.StatusCode code, Tny.StatusCallback status_callback, void* status_user_data);
		public abstract void stop_operation (bool cancelled);
		public signal void changed ();
		public signal void connection_status_changed (int status);
	}
	[CCode (cheader_filename = "tny.h")]
	public interface AccountStore : GLib.Object {
		public abstract bool alert (Tny.Account account, Tny.AlertType type, bool question, GLib.Error error);
		public abstract weak Tny.Account find_account (string url_string);
		public abstract void get_accounts (Tny.List list, Tny.GetAccountsRequestType types);
		public abstract weak string get_cache_dir ();
		public abstract weak Tny.Device get_device ();
		public signal void connecting_started ();
	}
	[CCode (cheader_filename = "tny.h")]
	public interface ConnectionPolicy {
		public abstract void on_connect (Tny.Account account);
		public abstract void on_connection_broken (Tny.Account account);
		public abstract void on_disconnect (Tny.Account account);
		public abstract void set_current (Tny.Account account, Tny.Folder folder);
	}
	[CCode (cheader_filename = "tny.h")]
	public interface Device : GLib.Object {
		public abstract void force_offline ();
		public abstract void force_online ();
		public abstract bool is_online ();
		public abstract void reset ();
		public signal void connection_changed (bool online);
	}
	[CCode (cheader_filename = "tny.h")]
	public interface Folder : GLib.Object {
		public abstract void add_msg (Tny.Msg msg) throws GLib.Error;
		public abstract void add_msg_async (Tny.Msg msg, Tny.FolderCallback callback, Tny.StatusCallback status_callback);
		public abstract void add_observer (Tny.FolderObserver observer);
		public abstract weak Tny.Folder copy (Tny.FolderStore into, string new_name, bool del) throws GLib.Error;
		public abstract void copy_async (Tny.FolderStore into, string new_name, bool del, Tny.CopyFolderCallback callback, Tny.StatusCallback status_callback);
		public abstract weak Tny.Msg find_msg (string url_string) throws GLib.Error;
		public abstract weak Tny.Account get_account ();
		public abstract uint get_all_count ();
		public abstract Tny.FolderCaps get_caps ();
		public abstract weak Tny.FolderStore get_folder_store ();
		public abstract Tny.FolderType get_folder_type ();
		public abstract void get_headers (Tny.List headers, bool refresh) throws GLib.Error;
		public abstract void get_headers_async (Tny.List headers, bool refresh, Tny.GetHeadersCallback callback, Tny.StatusCallback status_callback);
		public abstract weak string get_id ();
		public abstract uint get_local_size ();
		public abstract weak Tny.Msg get_msg (Tny.Header header) throws GLib.Error;
		public abstract void get_msg_async (Tny.Header header, Tny.GetMsgCallback callback, Tny.StatusCallback status_callback);
		public abstract weak Tny.MsgReceiveStrategy get_msg_receive_strategy ();
		public abstract weak Tny.MsgRemoveStrategy get_msg_remove_strategy ();
		public abstract weak string get_name ();
		public abstract weak Tny.FolderStats get_stats ();
		public abstract uint get_unread_count ();
		public abstract weak string get_url_string ();
		public abstract bool is_subscribed ();
		public abstract void poke_status ();
		public abstract void refresh () throws GLib.Error;
		public abstract void refresh_async ([CCode (delegate_target_pos = 2.1)] Tny.FolderCallback callback, Tny.StatusCallback status_callback);
		public abstract void remove_msg (Tny.Header header) throws GLib.Error;
		public abstract void remove_msgs (Tny.List headers) throws GLib.Error;
		public abstract void remove_msgs_async (Tny.List headers, Tny.FolderCallback callback, Tny.StatusCallback status_callback);
		public abstract void remove_observer (Tny.FolderObserver observer);
		public abstract void set_msg_receive_strategy (Tny.MsgReceiveStrategy st);
		public abstract void set_msg_remove_strategy (Tny.MsgRemoveStrategy st);
		public abstract void sync (bool expunge) throws GLib.Error;
		public abstract void sync_async (bool expunge, Tny.FolderCallback callback, Tny.StatusCallback status_callback);
		public abstract void transfer_msgs (Tny.List header_list, Tny.Folder folder_dst, bool delete_originals) throws GLib.Error;
		public abstract void transfer_msgs_async (Tny.List header_list, Tny.Folder folder_dst, bool delete_originals, Tny.TransferMsgsCallback callback, Tny.StatusCallback status_callback);
	}
	[CCode (cheader_filename = "tny.h")]
	public interface FolderObserver {
		public abstract void update (Tny.FolderChange change);
	}
	[CCode (cheader_filename = "tny.h")]
	public interface FolderStore {
		public abstract void add_observer (Tny.FolderStoreObserver observer);
		public abstract weak Tny.Folder create_folder (string name) throws GLib.Error;
		public abstract void create_folder_async (string name, Tny.CreateFolderCallback callback, Tny.StatusCallback status_callback);
		public abstract void get_folders (Tny.List list, Tny.FolderStoreQuery query) throws GLib.Error;
		public abstract void get_folders_async (Tny.List list, Tny.FolderStoreQuery query, Tny.GetFoldersCallback callback, Tny.StatusCallback status_callback);
		public abstract void remove_folder (Tny.Folder folder) throws GLib.Error;
		public abstract void remove_observer (Tny.FolderStoreObserver observer);
	}
	[CCode (cheader_filename = "tny.h")]
	public interface FolderStoreObserver {
		public abstract void update (Tny.FolderStoreChange change);
	}
	[CCode (cheader_filename = "tny.h")]
	public interface Header : GLib.Object {
		public Tny.HeaderFlags get_priority ();
		public void set_priority (Tny.HeaderFlags priority);
		public abstract string dup_bcc ();
		public abstract string dup_cc ();
		public abstract string dup_from ();
		public abstract string dup_message_id ();
		public abstract string dup_replyto ();
		public abstract string dup_subject ();
		public abstract string dup_to ();
		public abstract string dup_uid ();
		public abstract ulong get_date_received ();
		public abstract ulong get_date_sent ();
		public abstract Tny.HeaderFlags get_flags ();
		public abstract weak Tny.Folder get_folder ();
		public abstract uint get_message_size ();
		public abstract void set_bcc (string bcc);
		public abstract void set_cc (string cc);
		public abstract void set_flag (Tny.HeaderFlags mask);
		public abstract void set_from (string from);
		public abstract void set_replyto (string to);
		public abstract void set_subject (string subject);
		public abstract void set_to (string to);
		public abstract void unset_flag (Tny.HeaderFlags mask);
	}
	[CCode (cheader_filename = "tny.h")]
	public interface Iterator {
		public abstract void first ();
		public abstract weak GLib.Object get_current ();
		public abstract weak Tny.List get_list ();
		public abstract bool is_done ();
		public abstract void next ();
		public abstract void nth (uint nth);
		public abstract void prev ();
	}
	[CCode (cheader_filename = "tny.h")]
	public interface List {
		public abstract void append (GLib.Object item);
		public abstract weak Tny.List copy ();
		public abstract weak Tny.Iterator create_iterator ();
		public abstract void @foreach (GLib.Func func);
		public abstract uint get_length ();
		public abstract void prepend (GLib.Object item);
		public abstract void remove (GLib.Object item);
		public abstract void remove_matches (Tny.ListMatcher matcher, void* match_data);
	}
	[CCode (cheader_filename = "tny.h")]
	public interface Lockable {
		public abstract void @lock ();
		public abstract void unlock ();
	}
	[CCode (cheader_filename = "tny.h")]
	public interface MimePart {
		public abstract int add_part (Tny.MimePart part);
		public abstract int @construct (Tny.Stream stream, string mime_type, string transfer_encoding);
		public abstract bool content_type_is (string type);
		public abstract long decode_to_stream (Tny.Stream stream) throws GLib.Error;
		public abstract void decode_to_stream_async (Tny.Stream stream, Tny.MimePartCallback callback, Tny.StatusCallback status_callback);
		public abstract void del_part (Tny.MimePart part);
		public abstract weak string get_content_id ();
		public abstract weak string get_content_location ();
		public abstract weak string get_content_type ();
		public abstract weak Tny.Stream get_decoded_stream ();
		public abstract weak string get_description ();
		public abstract weak string get_filename ();
		public abstract void get_header_pairs (Tny.List list);
		public abstract void get_parts (Tny.List list);
		public abstract weak Tny.Stream get_stream ();
		public abstract weak string get_transfer_encoding ();
		public abstract bool is_attachment ();
		public abstract bool is_purged ();
		public abstract void set_content_id (string content_id);
		public abstract void set_content_location (string content_location);
		public abstract void set_content_type (string contenttype);
		public abstract void set_description (string description);
		public abstract void set_filename (string filename);
		public abstract void set_header_pair (string name, string value);
		public abstract void set_purged ();
		public abstract void set_transfer_encoding (string transfer_encoding);
		public abstract long write_to_stream (Tny.Stream stream) throws GLib.Error;
	}
	[CCode (cheader_filename = "tny.h")]
	public interface Msg : Tny.MimePart, GLib.Object {
		public abstract weak Tny.Folder get_folder ();
		public abstract weak Tny.Header get_header ();
		public abstract weak string get_url_string ();
		public abstract void rewrite_cache ();
		public abstract void uncache_attachments ();
	}
	[CCode (cheader_filename = "tny.h")]
	public interface MsgReceiveStrategy {
		public abstract weak Tny.Msg perform_get_msg (Tny.Folder folder, Tny.Header header) throws GLib.Error;
	}
	[CCode (cheader_filename = "tny.h")]
	public interface MsgRemoveStrategy {
		public abstract void perform_remove (Tny.Folder folder, Tny.Header header) throws GLib.Error;
	}
	[CCode (cheader_filename = "tny.h")]
	public interface PasswordGetter {
		public abstract void forget_password (string aid);
		public abstract weak string get_password (string aid, string prompt, bool cancel);
	}
	[CCode (cheader_filename = "tny.h")]
	public interface Seekable {
		public abstract int64 seek (int64 offset, int policy);
		public abstract int set_bounds (int64 start, int64 end);
		public abstract int64 tell ();
	}
	[CCode (cheader_filename = "tny.h")]
	public interface SendQueue : GLib.Object {
		public abstract void add (Tny.Msg msg) throws GLib.Error;
		public abstract void add_async (Tny.Msg msg, Tny.SendQueueAddCallback callback, Tny.StatusCallback status_callback);
		public abstract void cancel (Tny.SendQueueCancelAction cancel_action) throws GLib.Error;
		public abstract weak Tny.Folder get_outbox ();
		public abstract weak Tny.Folder get_sentbox ();
		public signal void error_happened (Tny.Header header, Tny.Msg msg, void* err);
		public signal void msg_sending (Tny.Header header, Tny.Msg msg, uint nth, uint total);
		public signal void msg_sent (Tny.Header header, Tny.Msg msg, uint nth, uint total);
		public signal void queue_start ();
		public signal void queue_stop ();
	}
	[CCode (cheader_filename = "tny.h")]
	public interface StoreAccount : Tny.FolderStore, Tny.Account, GLib.Object {
		public abstract void delete_cache ();
		public abstract weak Tny.Folder find_folder (string url_string) throws GLib.Error;
		public abstract void subscribe (Tny.Folder folder);
		public abstract void unsubscribe (Tny.Folder folder);
		public signal void subscription_changed (Tny.Folder folder);
	}
	[CCode (cheader_filename = "tny.h")]
	public interface Stream {
		public abstract int close ();
		public abstract int flush ();
		public abstract bool is_eos ();
		public abstract long read (string buffer, ulong n);
		public abstract int reset ();
		public abstract long write (string buffer, ulong n);
		public abstract long write_to_stream (Tny.Stream output);
	}
	[CCode (cheader_filename = "tny.h")]
	public interface TransportAccount : Tny.Account, GLib.Object {
		public abstract void send (Tny.Msg msg) throws GLib.Error;
	}
	[CCode (cheader_filename = "tny.h")]
	public delegate void CopyFolderCallback (Tny.Folder _self, bool cancelled, Tny.FolderStore into, Tny.Folder new_folder, GLib.Error err);
	[CCode (cheader_filename = "tny.h")]
	public delegate void CreateFolderCallback (Tny.FolderStore _self, bool cancelled, Tny.Folder new_folder, GLib.Error err);
	[CCode (cheader_filename = "tny.h")]
	public delegate void FolderCallback (Tny.Folder _self, bool cancelled, GLib.Error err);
	[CCode (cheader_filename = "tny.h")]
	public static delegate void ForgetPassFunc (Tny.Account _self);
	[CCode (cheader_filename = "tny.h")]
	public delegate void GetFoldersCallback (Tny.FolderStore _self, bool cancelled, Tny.List list, GLib.Error err);
	[CCode (cheader_filename = "tny.h")]
	public delegate void GetHeadersCallback (Tny.Folder _self, bool cancelled, Tny.List headers, GLib.Error err);
	[CCode (cheader_filename = "tny.h")]
	public delegate void GetMsgCallback (Tny.Folder folder, bool cancelled, Tny.Msg msg, GLib.Error err);
	[CCode (cheader_filename = "tny.h")]
	public static delegate weak string GetPassFunc (Tny.Account _self, string prompt, bool cancel);
	[CCode (cheader_filename = "tny.h")]
	public static delegate bool ListMatcher (Tny.List list, GLib.Object item, void* match_data);
	[CCode (cheader_filename = "tny.h")]
	public delegate void MimePartCallback (Tny.MimePart _self, bool cancelled, Tny.Stream stream, GLib.Error err);
	[CCode (cheader_filename = "tny.h")]
	public delegate void SendQueueAddCallback (Tny.SendQueue _self, bool cancelled, Tny.Msg msg, GLib.Error err);
	[CCode (cheader_filename = "tny.h")]
	public delegate void StatusCallback (GLib.Object _self, Tny.Status status);
	[CCode (cheader_filename = "tny.h")]
	public delegate void TransferMsgsCallback (Tny.Folder folder, bool cancelled, GLib.Error err);
	public const int HEADER_FLAG_PRIORITY_MASK;
	public const int PRIORITY_LOWER_THAN_GTK_REDRAWS;
	[CCode (cheader_filename = "tny.h")]
	public static void clear_status (out weak Tny.Status status);
	[CCode (cheader_filename = "tny.h")]
	public static int error_get_code (GLib.Error err);
	[CCode (cheader_filename = "tny.h")]
	public static weak string error_get_message (GLib.Error err);
	[CCode (cheader_filename = "tny.h")]
	public static void marshal_VOID__OBJECT_OBJECT_INT_INT (GLib.Closure closure, GLib.Value return_value, uint n_param_values, GLib.Value param_values, void* invocation_hint, void* marshal_data);
	[CCode (cheader_filename = "tny.h")]
	public static void marshal_VOID__OBJECT_OBJECT_POINTER (GLib.Closure closure, GLib.Value return_value, uint n_param_values, GLib.Value param_values, void* invocation_hint, void* marshal_data);
	[CCode (cheader_filename = "tny.h")]
	public static void set_status (out weak Tny.Status status, GLib.Quark domain, int code, uint position, uint of_total, string format);
}
