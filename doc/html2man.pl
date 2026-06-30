#!/usr/bin/perl -wT

# usage: hxnormalize -xe -l 99999 | hxpipe | html2man.pl

use strict;
use utf8;

use Data::Dumper;
use Getopt::Long;

binmode STDIN, ":utf8";
binmode STDOUT, ":utf8";

my $nroff = 0;

GetOptions(
	"nroff|n" => \$nroff,
);

# always allow
my %inline = (
	"strong" => 1,
	"b" => 1,
	"em" => 1,
	"i" => 1,
	"var" => 1,
	"code" => 1,
	"samp" => 1,
	"a" => 1,
	"kbd" => 1,
	"tt" => 1,
	"cite" => 1,
);

my %block = (
	"" => {
		"html" => 1,
	},
	"html" => {
		"head" => 1,
		"body" => 1,
	},
	"head" => {
		"title" => 1,
		"meta" => 1,
		"style" => 1,
	},
	"body" => {
		"h1" => 1,
		"h2" => 1,
		"h3" => 1,
		"h4" => 1,
		"p" => 1,
		"pre" => 1,
		"ul" => 1,
		"dl" => 1,
		"table" => 1,
		"div" => 1,
	},
	"ul" => {
		"li" => 1,
	},
	"dl" => {
		"dt" => 1,
		"dd" => 1,
	},
	"dd" => {
		"p" => 1,
	},
	"table" => {
		"thead" => 1,
		"tbody" => 1,
		"tr" => 1,
	},
	"thead" => {
		"tr" => 1,
	},
	"tbody" => {
		"tr" => 1,
	},
	"tr" => {
		"th" => 1,
		"td" => 1,
	},
	"p" => {
	},
);

my %handler = (
	"body" => [ \&o_body, \&c_body ],
	"h1" => [ undef, \&c_h1 ],
	"h2" => [ undef, \&c_h2 ],
	"h3" => [ undef, \&c_h3 ],
	"h4" => [ undef, \&c_h4 ],
	"pre" => [ \&o_pre, \&c_pre ],
	"p" => [ \&o_p, \&c_p ],
	"dt" => [ undef, \&c_dt ],
	"dd" => [ undef, \&c_dd ],
	"li" => [ undef, \&c_li ],
	"strong" => [ \&o_strong, \&c_strong ],
	"b" => [ \&o_strong, \&c_strong ],
	"code" => [ \&o_code, \&c_code ],
	"em" => [ \&o_em, \&c_em ],
	"i" => [ \&o_em, \&c_em ],
	"var" => [ \&o_em, \&c_em ],
	"a" => [ \&o_a, \&c_a ],
	"table" => [ \&o_table, \&c_table ],
	"thead" => [ \&o_thead, undef ],
	"tbody" => [ \&o_tbody, undef ],
	"tr" => [ \&o_tr, undef ],
	"th" => [ undef, \&c_th ],
	"td" => [ undef, \&c_td ],
);

my @stack = ();
my $top = { 'tag' => '', 'elist' => [] };
push @stack, $top;
my $current = $top;
my $attrs = {};

sub close_tag {
	my $tag = shift;
	while ($tag ne $current->{'tag'}) {
		$current = pop @stack;
		return unless @stack;
		$current = pop @stack;
		push @stack, $current;
	}
	$current = pop @stack;
	return unless @stack;
	$current = pop @stack;
	push @stack, $current;
}

while (<>) {
	chomp;
	my $cmd = substr($_, 0, 1);
	my $arg = substr($_, 1);
	if ($cmd eq 'A') {
		my ($a, $type, $data) = split(/ /, $arg, 3);
		$attrs->{$a} = $data;
	} elsif ($cmd eq '(') {
		my $tag = $arg;
		if (!exists $inline{$tag}) {
			my $trace = join(" ", map { "<".$_->{'tag'}.">" } @stack);
			my $parent = $current;
			my $parent_tag = $parent->{'tag'};
			if (exists $block{$parent_tag}) {
				while (@stack && !exists $block{$parent_tag}->{$tag}) {
					close_tag($parent_tag);
					$parent = $current;
					$parent_tag = $parent->{'tag'};
				}
				if (!@stack) {
					print STDERR "<$tag> not inline or allowed anywhere in:\n    $trace\n";
					exit 1;
				}
			}
		}
		my $parent = $current;
		my $list = $parent->{'elist'};
		$current = { 'tag' => $tag, 'attrs' => $attrs, 'elnum' => scalar(@{$list}), 'parent' => $parent, 'elist' => [] };
		push @{$list}, $current;
		push @stack, $current;
		$attrs = {};
	} elsif ($cmd eq ')') {
		my $tag = $arg;
		close_tag($tag);
	} elsif ($cmd eq '-') {
		my $parent = $current;
		my $list = $parent->{'elist'};
		my $text .= from_html($arg);
		my $elem = { 'text' => $text, 'elnum' => scalar(@{$list}), 'parent' => $parent };
		push @{$list}, $elem;
	}
}

# - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

my $ctx = { 'font' => '\\fR' };

parse_elem($ctx, $top);

exit 0;

# - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

sub prev_elem {
	my ($ctx, $elem) = @_;
	return undef unless exists $elem->{'elnum'};
	my $elnum = $elem->{'elnum'} - 1;
	my $parent = $elem->{'parent'};
	return undef if $elnum < 0;
	return undef if $elnum >= scalar(@{$parent->{'elist'}});
	return $parent->{'elist'}->[$elnum];
}

sub next_elem {
	my ($ctx, $elem) = @_;
	return undef unless exists $elem->{'elnum'};
	my $elnum = $elem->{'elnum'} + 1;
	my $parent = $elem->{'parent'};
	return undef if $elnum < 0;
	return undef if $elnum >= scalar(@{$parent->{'elist'}});
	return $parent->{'elist'}->[$elnum];
}

sub prev_tag {
	my ($ctx, $elem) = @_;
	my $prev = $elem;
	while ($prev = prev_elem($ctx, $prev)) {
		return $prev->{'tag'} if exists $prev->{'tag'};
	}
	return "";
}

sub prev_word {
	my ($ctx, $elem) = @_;
	my $word = "";
	if ($ctx->{'text'} =~ /^(.*)?\s*(\S+)$/s) {
		$ctx->{'text'} = $1;
		$word = $2;
	}
	return $word;
}

sub next_word {
	my ($ctx, $elem) = @_;
	my $next = next_elem($ctx, $elem);
	return "" unless defined $next;
	return "" if exists $next->{'tag'};
	return "" unless exists $next->{'text'};
	my $word = "";
	if ($next->{'text'} =~ /^(\S+)\s*(.*)$/) {
		$word = $1;
		$next->{'text'} = $2;
	}
	return $word;
}

sub parse_elem {
	my ($ctx, $elem) = @_;

	if (exists $elem->{'tag'}) {
		my $tag = $elem->{'tag'};
		my $parent = $elem->{'parent'};
		my $elnum = $elem->{'elnum'};
		if (exists $handler{$tag} && defined $handler{$tag}->[0]) {
			&{$handler{$tag}->[0]}($ctx, $elem);
		}
		if (exists $elem->{'elist'}) {
			for my $e (@{$elem->{'elist'}}) {
				parse_elem($ctx, $e);
			}
		}
		if (exists $handler{$tag} && defined $handler{$tag}->[1]) {
			&{$handler{$tag}->[1]}($ctx, $elem);
		}
	} else {
		my $text = to_man($elem->{'text'});
		if ($ctx->{'output_enabled'}) {
			if ($ctx->{'monospace'}) {
				$text =~ s/-/\\-/g;
				$text =~ s/\^/\\(ha/g;
				$text =~ s/~/\\(ti/g;
			}
			if ($ctx->{'verbatim'}) {
				$text =~ s/ /\\ /g;
				$text =~ s/-/\\-/g;
			} else {
				if ($elem->{'elnum'} == 0) {
					$text =~ s/^\s*//s;
				}
				$text =~ s/\s+/ /s;
			}
			$ctx->{'text'} .= $text;
		}
	}
}

sub prune_text {
	my ($ctx, $elem) = @_;
	my $text = $ctx->{'text'};
	$ctx->{'text'} = "";
	if ($ctx->{'verbatim'}) {
		$text =~ s/^\s*\n*//gs;
		$text =~ s/\s*\n/\n/gs;
		$text =~ s/\n*$//gs;
	} else {
		$text =~ s/^\s*//s;
		$text =~ s/\s*$//s;
	}
	return $text;
}

sub o_body {
	my ($ctx, $elem) = @_;
	$ctx->{'output_enabled'} = 1;
}

sub c_body {
	my ($ctx, $elem) = @_;
	$ctx->{'output_enabled'} = 0;
}

sub c_h1 {
	my ($ctx, $elem) = @_;
	my $text = prune_text($ctx);
	my $attrs = $elem->{'attrs'};
	my $date = $attrs->{'man:date'} // "";
	my $section = $attrs->{'man:section'} // "";
	my $dist = $attrs->{'man:dist'} // "";
	print ".TH \"$text\" \"$section\" \"$date\" \"$dist\"\n";
	print ".hy 0\n";  # no hyphenation
	print ".nh\n";  # no hyphenation
	#print ".ad l\n";  # left justify
}

sub c_h2 {
	my ($ctx, $elem) = @_;
	my $text = prune_text($ctx);
	print ".H1 $text\n";
}

sub c_h3 {
	my ($ctx, $elem) = @_;
	my $text = prune_text($ctx);
	print ".H2 $text\n";
}

sub c_h4 {
	my ($ctx, $elem) = @_;
	my $text = prune_text($ctx);
	print ".H3 $text\n";
}

sub o_pre {
	my ($ctx, $elem) = @_;
	$ctx->{'verbatim'} = 1;
}

sub c_pre {
	my ($ctx, $elem) = @_;
	my $text = prune_text($ctx);
	print ".IP\n";
	print ".EX\n";
	print "$text\n";
	print ".EE\n";
	$ctx->{'verbatim'} = 0;
}

sub o_p {
	my ($ctx, $elem) = @_;
	my $ptag = $elem->{'parent'}->{'tag'};
	if ($ptag eq 'dd') {
		c_dd($ctx, $elem);
	}
}

sub c_p {
	my ($ctx, $elem) = @_;
	my $text = prune_text($ctx);
	my $ptag = $elem->{'parent'}->{'tag'};
	if ($ptag eq 'dd') {
		if (!$ctx->{'suppress_ip'}) {
			print ".IP\n";
		}
	} else {
		print ".PP\n";
	}
	print "$text\n";
}

sub c_dt {
	my ($ctx, $elem) = @_;
	my $text = prune_text($ctx);
	my $ptag = prev_tag($ctx, $elem);
	if ($ptag eq 'dt') {
		print ".TQ\n";
	} else {
		print ".TP\n";
	}
	print "$text\n";
}

sub c_dd {
	my ($ctx, $elem) = @_;
	my $text = prune_text($ctx);
	my $ptag = prev_tag($ctx, $elem);
	$ctx->{'suppress_ip'} = ($text eq '');
	if (!$ctx->{'suppress_ip'}) {
		print "$text\n";
	}
}

sub c_li {
	my ($ctx, $elem) = @_;
	my $text = prune_text($ctx);
	my $ptag = prev_tag($ctx, $elem);
	print ".IP \\(bu 2\n";
	print "$text\n";
}

sub pushfont {
	my ($ctx, $elem, $font) = @_;
	$ctx->{'text'} .= $font;
	push @{$ctx->{'fontstack'}}, $ctx->{'font'};
	$ctx->{'font'} = "\\fB";
}

sub popfont {
	my ($ctx, $elem) = @_;
	$ctx->{'font'} = pop @{$ctx->{'fontstack'}};
	$ctx->{'text'} .= $ctx->{'font'};
}

sub o_strong {
	my ($ctx, $elem) = @_;
	pushfont($ctx, $elem, "\\fB");
}

sub c_strong {
	my ($ctx, $elem) = @_;
	popfont($ctx, $elem);
}

sub o_code {
	my ($ctx, $elem) = @_;
	$ctx->{'monospace'} = 1;
	pushfont($ctx, $elem, "\\f(CB");
}

sub c_code {
	my ($ctx, $elem) = @_;
	$ctx->{'monospace'} = 0;
	popfont($ctx, $elem);
}

sub o_em {
	my ($ctx, $elem) = @_;
	pushfont($ctx, $elem, "\\fI");
}

sub c_em {
	my ($ctx, $elem) = @_;
	popfont($ctx, $elem);
}

sub o_a {
	my ($ctx, $elem) = @_;
	my $attrs = $elem->{'attrs'};
	return if !exists $attrs->{'href'};
	my $href = $attrs->{'href'};
	return if ($href =~ /^#/);
	$ctx->{'urlhref'} = $href;
	$ctx->{'urlpre'} = prev_word($ctx, $elem);
	$ctx->{'urlsave'} = $ctx->{'text'};
	$ctx->{'text'} = "";
}

sub c_a {
	my ($ctx, $elem) = @_;
	return if (!exists $ctx->{'urlhref'});
	my $save = $ctx->{'urlsave'};
	my $pre = $ctx->{'urlpre'};
	my $href = $ctx->{'urlhref'};
	my $post = next_word($ctx, $elem);
	my $text = $ctx->{'text'};
	$ctx->{'text'} = "$save";
	if ($text ne $href) {
		$ctx->{'text'} .= " $text";
		$pre .= "(";
		$post = ")$post";
	}
	$ctx->{'text'} .= "\n.UU \"$pre\" \"$href\" \"$post\"\n";
	delete $ctx->{'urlsave'};
	delete $ctx->{'urlpre'};
	delete $ctx->{'urlhref'};
}

sub o_table {
	my ($ctx, $elem) = @_;
	my $attrs = $elem->{'attrs'};
	my $tab = $attrs->{'tab'} // ';';
	my $align = $attrs->{'align'} // 'l l';
	$ctx->{'tbltab'} = $tab;
	$ctx->{'tblsep'} = ($align =~ /\|/) ? ' | ' : ' ';
	$ctx->{'tblalign'} = $attrs->{'align'};
	$ctx->{'tblcolumns'} = scalar(split(/[ |]+/, $align));
	print ".P\n";
	print ".TS\n";
	print "tab($tab);\n";
	$ctx->{'tblformatted'} = 0;
}

sub o_thead {
	my $tab = $ctx->{'tbltab'};
	my $align = $ctx->{'tblalign'};
	my $columns = $ctx->{'tblcolumns'};

	if ($ctx->{'tblformatted'}) {
		print ".T&\n";
		print join($ctx->{'tblsep'}, ("_") x $columns)."\n";
	}
	print "${align}.\n";
	$ctx->{'tblformatted'} = 1;
}

sub o_tbody {
	my $tab = $ctx->{'tbltab'};
	my $align = $ctx->{'tblalign'};
	my $columns = $ctx->{'tblcolumns'};
	if ($ctx->{'tblformatted'}) {
		print ".T&\n";
		print join($ctx->{'tblsep'}, ("_") x $columns)."\n";
	}
	print "${align}.\n";
	$ctx->{'tblformatted'} = 1;
}

sub c_table {
	my ($ctx, $elem) = @_;
	print ".TE\n";
	$ctx->{'text'} = "";
}

sub o_tr {
	my ($ctx, $elem) = @_;
	$ctx->{'tblcolumn'} = 0;
	$ctx->{'text'} = "";
}

sub c_th {
	my ($ctx, $elem) = @_;
	my $text = prune_text($ctx);
	my $column = $ctx->{'tblcolumn'};
	my $tab = $ctx->{'tbltab'};
	print $tab if ($column > 0);
	$column++;
	print "T{\n" if ($column eq $ctx->{'tblcolumns'});
	print "\\fB$text".$ctx->{'font'};
	if ($column eq $ctx->{'tblcolumns'}) {
		print "\nT}\n";
	}
	$ctx->{'tblcolumn'} = $column;
}

sub c_td {
	my ($ctx, $elem) = @_;
	my $text = prune_text($ctx);
	my $column = $ctx->{'tblcolumn'};
	my $tab = $ctx->{'tbltab'};
	print $tab if ($column > 0);
	$column++;
	print "T{\n" if ($column eq $ctx->{'tblcolumns'});
	print "$text";
	if ($column eq $ctx->{'tblcolumns'}) {
		print "\nT}\n";
	}
	$ctx->{'tblcolumn'} = $column;
}

# - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

sub from_html {
	my $t = shift;
	$t =~ s/%/%25/g;
	$t =~ s/&amp;/%26/g;
	$t =~ s/&mdash;/—/g;
	$t =~ s/&ldquo;/“/g;
	$t =~ s/&rdquo;/”/g;
	$t =~ s/&lsquo;/‘/g;
	$t =~ s/&rsquo;/’/g;
	$t =~ s/&hellip;/…/g;
	$t =~ s/&lt;/</g;
	$t =~ s/&gt;/>/g;
	$t =~ s/&nbsp;/\xa0/g;
	$t =~ s/%([0-9a-f]{2})/pack("c",hex($1))/ge;
	$t =~ s/\\n/\n/g;
	$t =~ s/\\\\/\\/g;
	return $t;
}

sub to_man {
	my $t = shift;
	$t =~ s/\\/\\\\/g;
	if ($nroff) {
		$t =~ s/—/ \\- /g;
		$t =~ s/“/"/g;
		$t =~ s/”/"/g;
		$t =~ s/‘/'/g;
		$t =~ s/’/'/g;
		$t =~ s/…/.../g;
	} else {
		# protect, %, use it to escape brackets
		$t =~ s/%/%25/g;
		$t =~ s/\[/%5b/g;
		$t =~ s/\]/%5d/g;
		$t =~ s/%5b/\\[lB]/g;
		$t =~ s/%5d/\\[rB]/g;
		$t =~ s/%([0-9a-f]{2})/pack("c",hex($1))/ge;

		$t =~ s/—/\\[em]/g;
		$t =~ s/“/\\[lq]/g;
		$t =~ s/”/\\[rq]/g;
		$t =~ s/‘/\\[oq]/g;
		$t =~ s/’/\\[cq]/g;
		$t =~ s/…/\\[...]/g;

		$t =~ s/'/\\[aq]/g;  # apostrophe quote, not curly
		$t =~ s/\^/\\[ha]/g;  # different glyph for hat
		$t =~ s/~/\\[ti]/g;  # different glyph for tilde
		$t =~ s/\xa0/\\~/g;  # non-breaking space
	}
	return $t;
}
