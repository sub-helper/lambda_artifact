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

public class sudf_8 {

  private static String m_url = "jdbc:default:connection";
  private static String remove_trailingspace_regex = "\\s+$";
  private static String previousResult = "";
  private static Boolean hasResult = false;

  public static String evaluation() throws SQLException {
    String allManag = "";
    Connection conn = DriverManager.getConnection(m_url);
    String query1 =
      "select i_class, count(i_class) as cnt from catalog_returns, item where i_item_sk = cr_item_sk group by i_class";
    PreparedStatement stmt1 = conn.prepareStatement(query1);
    ResultSet rs1 = stmt1.executeQuery();
    int maxReturn = 0;
    String maxClass = "";

    while (rs1.next()) {
      // non-empty result processing
      // very necessary
      // Need to deal a case wherein i_manufact is null
      String i_class = rs1.getString("i_class");
      Integer count = rs1.getInt(2);
      if (count == null) {
        count = Integer.valueOf(0);
      }
      if (i_class == null) {
        i_class = "";
      }
      if (count > maxReturn) {
        maxClass = i_class;
        maxReturn = count;
      }
    }

    return maxClass;
  }

  @Function
  public static String sudf_8_maxretclass() throws SQLException {
    return evaluation();
  }
  @Function(parallel = Parallel.SAFE)
  public static String sudf_8_maxretclass_safe() throws SQLException {
    return evaluation();
  }

  @Function
  public static String sudf_8_maxretclass_cache() throws SQLException {
    if (hasResult) {
      return previousResult;
    } else {
      String result = evaluation();
      hasResult = true;
      previousResult = result;
      return result;
    }
  }
}
