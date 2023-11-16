package org.example.pgsql_udf;

import static org.postgresql.pljava.annotation.Function.Effects;
import static org.postgresql.pljava.annotation.Function.OnNullInput;
import static org.postgresql.pljava.annotation.Function.Parallel;

import java.io.File;
import java.io.FileNotFoundException;
import java.io.PrintStream;
import java.lang.annotation.RetentionPolicy;
import java.sql.Connection;
import java.sql.DriverManager;
import java.sql.PreparedStatement;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Random;
import java.util.Set;
import org.postgresql.pljava.annotation.Function;
import org.postgresql.pljava.annotation.Function.Effects;

public class sudf_7 {

  private static String m_url = "jdbc:default:connection";
  private static String remove_trailingspace_regex = "\\s+$";
  private static HashMap<String, Integer> cache = new HashMap<String, Integer>();

  public static Integer evaluation(String manager, Integer yr)
    throws SQLException {
    Double result = null;
    String query =
      "SELECT SUM(ss_net_profit) FROM store, store_sales_history, date_dim WHERE ss_sold_date_sk = d_date_sk AND d_year = ? AND s_manager = ? AND s_store_sk = ss_store_sk";
    Connection conn = DriverManager.getConnection(m_url);
    PreparedStatement stmt = conn.prepareStatement(query);
    stmt.setInt(1, yr);
    stmt.setString(2, manager);
    ResultSet rs = stmt.executeQuery();
    if (rs.next()) {
      // non-empty result processing
      // very necessary
      // Need to deal a case wherein i_manufact is null
      result = rs.getDouble(1);
      if (result != null) {
        result = Double.valueOf(0);
      }
    }
    stmt.close();
    conn.close();
    if (result > 0) {
      return 1;
    } else {
      return 0;
    }
  }

  @Function
  public static Integer sudf_7_profitableManager(String manager, Integer yr)
    throws SQLException {
    if (yr == null || manager == null) {
      return null;
    }
    return evaluation(manager, yr);
  }
  @Function(onNullInput = OnNullInput.RETURNS_NULL)
  public static Integer sudf_7_profitableManager_null(String manager, Integer yr)
    throws SQLException {
    if (yr == null || manager == null) {
      return null;
    }
    return evaluation(manager, yr);
  }
  @Function(parallel = Parallel.SAFE)
  public static Integer sudf_7_profitableManager_safe(String manager, Integer yr)
    throws SQLException {
    if (yr == null || manager == null) {
      return null;
    }
    return evaluation(manager, yr);
  }

  @Function
  public static Integer sudf_7_profitableManager_cache(
    String manager,
    Integer yr
  ) throws SQLException {
    if (yr == null || manager == null) {
      return null;
    }

    String key = manager + yr.toString();
    if (cache.containsKey(key)) {
      return cache.get(key);
    } else {
      Integer result = evaluation(manager, yr);
      cache.put(key, result);
      return result;
    }
  }
}
