use lib "$Bin/lib";
use JSON;

@ARGV < 2 and die "Usage: $0 <prefix> <file>\n";
my $prefix = shift @ARGV;

our $ctl;
our %tlv_types = (
	gint8 => "int8_t",
	guint8 => "uint8_t",
	gint16 => "int16_t",
	guint16 => "uint16_t",
	gint32 => "int32_t",
	guint32 => "uint32_t",
	gint64 => "int64_t",
	guint64 => "uint64_t",
	gfloat => "float",
	gboolean => "bool",
);
our %common_ref = ();

my @c_reserved_keywords = (
	"alignas",
	"alignof",
	"auto",
	"bool",
	"break",
	"case",
	"char",
	"const",
	"constexpr",
	"continue",
	"default",
	"do",
	"double",
	"else",
	"enum",
	"extern",
	"false",
	"float",
	"for",
	"goto",
	"if",
	"inline",
	"int",
	"long",
	"nullptr",
	"register",
	"restrict",
	"return",
	"short",
	"signed",
	"sizeof",
	"static",
	"static_assert",
	"struct",
	"switch",
	"thread_local",
	"true",
	"typedef",
	"typeof",
	"typeof_unqual",
	"union",
	"unsigned",
	"void",
	"volatile",
	"while"
);

$prefix eq 'ctl_' and $ctl = 1;

sub get_json() {
	local $/;
	my $json = <>;
	$json =~ s/^\s*\/\/.*$//mg;
	return decode_json($json);
}

sub gen_cname($) {
	my $name = shift;

	$name =~ s/[^a-zA-Z0-9_]/_/g;
	$name = "_${name}" if $name =~ /^\d/;
	$name = lc($name);
	$name = "_${name}" if (grep {$_ eq $name} @c_reserved_keywords);
	return $name;
}

sub gen_has_types($) {
	my $data = shift;

	foreach my $field (@$data) {
		$field = gen_common_ref($field);
		my $type = $field->{"format"};
		$type and return 1;
	}
	return undef
}

sub gen_tlv_set_func($$) {
	my $name = shift;
	my $data = shift;

	$name = gen_cname($name);
	if (gen_has_types($data)) {
		return "int qmi_set_$name(struct qmi_msg *msg, struct qmi_$name *req)"
	} else {
		return "int qmi_set_$name(struct qmi_msg *msg)"
	}
}

sub gen_tlv_parse_func($$) {
	my $name = shift;
	my $data = shift;

	$name = gen_cname($name);
	if (gen_has_types($data)) {
		return "int qmi_parse_$name(struct qmi_msg *msg, struct qmi_$name *res)"
	} else {
		return "int qmi_parse_$name(struct qmi_msg *msg)"
	}
}

sub gen_common_ref($$) {
	my $field = shift;
	$field = $common_ref{$field->{'common-ref'}} if $field->{'common-ref'} ne '';
	return $field;
}

sub gen_foreach_message_type($$$)
{
	my $data = shift;
	my $req_sub = shift;
	my $res_sub = shift;
	my $ind_sub = shift;

	foreach my $entry (@$data) {
		my $args = [];
		my $fields = [];

		$common_ref{$entry->{'common-ref'}} = $entry if $entry->{'common-ref'} ne '';

		next if $entry->{type} ne 'Message';
		next if not defined $entry->{input} and not defined $entry->{output};

		&$req_sub($prefix.$entry->{name}." Request", $entry->{input}, $entry);
		&$res_sub($prefix.$entry->{name}." Response", $entry->{output}, $entry);
	}

	foreach my $entry (@$data) {
		my $args = [];
		my $fields = [];

		$common_ref{$entry->{'common-ref'}} = $entry if $entry->{'common-ref'} ne '';

		next if $entry->{type} ne 'Indication';
		next if not defined $entry->{input} and not defined $entry->{output};

		&$ind_sub($prefix.$entry->{name}." Indication", $entry->{output}, $entry);
	}
}


1;
