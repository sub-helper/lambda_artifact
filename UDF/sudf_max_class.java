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

public class sudf_max_class {
    private static String m_url = "jdbc:default:connection";
    private static String remove_trailingspace_regex = "\\s+$";

    private static HashMap<String, Double> cache = new HashMap<String, Double>();

    @Function
    public static Double sudf_max(Double input1, Double input2, Double input3) throws SQLException {
        return evaluate_max((Object[]) new Double[] { input1, input2, input3 });
    }
    @Function(onNullInput = OnNullInput.RETURNS_NULL)
    public static Double sudf_max_null(Double input1, Double input2, Double input3) throws SQLException {
        return evaluate_max((Object[]) new Double[] { input1, input2, input3 });
    }

    @Function(parallel = Parallel.SAFE)
    public static Double sudf_max_safe(Double input1, Double input2, Double input3) throws SQLException {
        return evaluate_max((Object[]) new Double[] { input1, input2, input3 });
    }
    @Function
    public static Double sudf_max_cache(Double input1, Double input2, Double input3) throws SQLException {
        if (input1 == null || input2 == null || input3 == null) {
            return null;
        }
        String key = input1.toString() + input2.toString() + input3.toString();
        if (cache.containsKey(key)) {
            return cache.get(key);
        } else {
            Double result = evaluate_max((Object[]) new Double[] { input1, input2, input3 });
            cache.put(key, result);
            return result;
        }
    }
    

    private static Double evaluate_max(Object[] data) {
        double max = 0.0;
        for (int i = 0; i < data.length; i++) {
            double val = 0.0;
            if (data[i] != null & data[i] instanceof Double) {
                val = ((Double) data[i]).doubleValue();
                if (max < val) {
                    max = val;
                }
            } else {
                return null;
            }
        }
        return Double.valueOf(max);
    }
}