package org.example.pgsql_udf;

import java.text.DateFormat;
import java.text.ParseException;
import java.text.SimpleDateFormat;
import java.util.Calendar;
import java.util.Date;
import java.sql.Connection;
import java.sql.DriverManager;
import java.sql.PreparedStatement;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.io.PrintStream;
import java.io.File;
import java.io.FileNotFoundException;
import java.util.HashSet;
import java.util.Random;
import java.util.Set;
import java.util.LinkedHashSet;
import java.util.Set;
import org.postgresql.pljava.annotation.Function;
import org.postgresql.pljava.annotation.Function.Effects;
import java.util.SortedSet;
import java.util.TreeSet;
import java.lang.Object;
import java.lang.Double;
import java.time.*;
import static org.postgresql.pljava.annotation.Function.Effects;
import static org.postgresql.pljava.annotation.Function.Parallel;
import static org.postgresql.pljava.annotation.Function.OnNullInput;
import java.util.HashMap;

public class sudf_string_class {
    private static String m_url = "jdbc:default:connection";
    private static String remove_trailingspace_regex = "\\s+$";

    private static HashMap<String, String> cache = new HashMap<String, String>();
    @Function
    public static String sudf_string_manipulation(String str, String split1, String split2) throws SQLException {
        return evaluate_string(str, split1, split2);
    }
        @Function
    public static String sudf_string_manipulation_cache(String str, String split1, String split2) throws SQLException {
        if (str == null || str.length() == 0)
            return "";
        if (split1 == null || split1.length() == 0)
            return "";
        if (split2 == null || split2.length() == 0)
            return "";
        String key = str.toString() + split1.toString() + split2.toString();
        if (cache.containsKey(key)) {
            return cache.get(key);
        } else {
            String result = evaluate_string(str, split1, split2);
            cache.put(key, result);
            return result;
        }

    }
    @Function(parallel = Parallel.SAFE)
    public static String sudf_string_manipulation_safe(String str, String split1, String split2) throws SQLException {
        return evaluate_string(str, split1, split2);
    }

    private static String evaluate_string(String str, String split1, String split2) {
        SortedSet<String> setOfKeys = new TreeSet<String>();
        if (str == null || str.length() == 0)
            return "";
        if (split1 == null || split1.length() == 0)
            return "";
        if (split2 == null || split2.length() == 0)
            return "";
        String[] pairArrary = str.split(split1);
        for (String item : pairArrary) {
            String[] pair = item.split(split2);
            String key = pair[0];
            setOfKeys.add(key);
        }
        if (setOfKeys.size() == 0) {
            return "";
        }
        StringBuffer result = new StringBuffer("");
        for (String key : setOfKeys)
            result.append(key);
        return result.toString();
    }
}