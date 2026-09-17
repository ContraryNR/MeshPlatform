package com.contrarynr.mesh;
import java.util.Collections;
import java.util.LinkedHashSet;
import java.util.Set;
public record ReportConfig(boolean enabled, int intervalMs, Set<String> fields)
{
    public static final Set<String> ALLOWED_FIELDS = Set.of("rtt", "traffic", "buffered", "state", "ice", "path");
    public ReportConfig
    {
        fields = Collections.unmodifiableSet(new LinkedHashSet<>(fields));
    }
    public static Set<String> parseFields(String s)
    {
        Set<String> set = new LinkedHashSet<>();
        for (String p : s.split(","))
            if (!p.isBlank())
                set.add(p.trim());
        return set;
    }
}
